#include "arlib.h"
#include <fcntl.h>
#include <sys/sendfile.h>

static const char * timefmt = "%Y-%m-%d %H:%M:%S";
static time_t parse_time(cstrnul str)
{
	struct tm tm = {};
	tm.tm_isdst = -1; // strptime won't set this
	strptime(str, timefmt, &tm);
	return mktime(&tm);
}
static string serialize_time(time_t t)
{
	char buf[128];
	strftime(buf, sizeof(buf), timefmt, localtime(&t));
	return buf;
}

struct group {
	string dir_a;
	string dir_b;
	string direction;
	array<string> dirs;
	
	template<typename T>
	void serialize(T& s)
	{
		s.items(
			"a", dir_a,
			"b", dir_b,
			"direction", ser_include_if_true(direction),
			"dirs", dirs);
	}
	
	bool is_ignored(cstring path)
	{
		string real_path = path;
		while (real_path.contains("//"))
			real_path = real_path.replace("//", "/");
		if (real_path.endswith("/"))
			real_path = real_path.substr(0, ~1);
		if (real_path.endswith("/.git"))
			return true;
		for (cstring sub : dirs)
		{
			if (sub[0] != '!')
				continue;
			sub = sub.substr(1, ~0);
			if (sub.endswith("/"))
				sub = sub.substr(0, ~1);
			if (real_path == sub)
				return true;
		}
		return false;
	}
};

struct cli_config {
	bool dry_run;
	bool direction_push;
	bool direction_pull;
};
struct config {
	time_t last_run;
	time_t current_time;
	size_t n_files_seen;
	size_t n_files_total;
	string ssh[2];
	array<group> groups;
	
	template<typename T>
	void serialize(T& s)
	{
		string last_run_s;
		if (s.serializing)
			last_run_s = serialize_time(current_time);
		s.items(
			"last_run", last_run_s,
			"n_files", (s.serializing ? n_files_seen : n_files_total),
			"ssh", ssh,
			"groups", groups);
		if (!s.serializing)
			last_run = parse_time(last_run_s);
	}
};

config cfg;
cli_config cfg_cli;

enum newer_t {
	n_neither = 0,
	n_dir1 = 1,
	n_dir2 = 2,
	n_equal = 3,
};

static newer_t is_newer(group& g, const struct statx & stat1, const struct statx & stat2)
{
	if (stat1.stx_mtime.tv_sec == stat2.stx_mtime.tv_sec && stat1.stx_size == stat2.stx_size)
		return n_equal;
	
	if (cfg_cli.direction_push)
		return n_dir1;
	if (cfg_cli.direction_pull)
		return n_dir2;
	if (g.direction == "push")
		return n_dir1;
	if (g.direction == "pull")
		return n_dir2;
	
	if (stat1.stx_mode == 0 || stat2.stx_mode == 0)
	{
		// if one file is gone, the other will be deleted only if
		// - mtime was at least 20 hours ago
		// - ctime was at least 20 hours ago (if not, it was probably renamed)
		// - btime was at least 20 hours ago (if not, it was probably downloaded and backdated)
		// - the above three are also prior to last sync
		const struct statx & st = (stat1.stx_mode ? stat1 : stat2);
		// btime isn't properly supported by neither sshfs nor find -printf %B@
		time_t file_time = max(st.stx_mtime.tv_sec, st.stx_ctime.tv_sec, st.stx_btime.tv_sec);
		bool do_delete = (file_time < min(cfg.current_time - 20*3600, cfg.last_run));
		if (do_delete)
			return stat1.stx_mode ? n_dir2 : n_dir1;
		else
			return stat1.stx_mode ? n_dir1 : n_dir2;
	}
	
	if (cfg.last_run >= 0 && cfg.last_run < stat1.stx_mtime.tv_sec && cfg.last_run < stat2.stx_mtime.tv_sec &&
		!S_ISDIR(stat1.stx_mode) && !S_ISDIR(stat2.stx_mode))
		return n_neither;
	if (stat1.stx_mtime.tv_sec < stat2.stx_mtime.tv_sec)
		return n_dir2;
	else
		return n_dir1;
}

template<typename T>
void listdir(cstrnul path, const T& callback)
{
	DIR* dir = opendir(path);
	if (!dir)
	{
		perror("opendir");
		puts(path);
		exit(1);
	}
	while (dirent* dent = readdir(dir))
	{
		if (dent->d_name[0] == '.')
		{
			if (dent->d_name[1] == '\0') continue;
			if (dent->d_name[1] == '.' && dent->d_name[2] == '\0') continue;
		}
		callback(dir, dent->d_name);
	}
	closedir(dir);
}


struct dir_entry {
	string name;
	struct statx stx;
};
static map<string, array<dir_entry>> statx_cache;
static string statx_prefix;

static void statx_parse_time(cstring str, struct statx_timestamp * out)
{
	array<cstring> parts = str.csplit<1>(".");
	fromstring(parts[0], out->tv_sec);
	fromstring(parts[1], out->tv_nsec);
}

static void statx_cache_create(cstring path, cstring local, cstring remote_addr)
{
	statx_cache.reset();
	if (!local)
		return;
	statx_prefix = local;
	
	string remote_dir = "";
	if (path.startswith(local))
		remote_dir = "/"+path.substr(local.length(), ~0);
	else
		return;
	FILE* f = popen("ssh "+remote_addr+" find '"+remote_dir.replace(">","\\>")+"' -printf '%y:%s:%T@:%C@:%B@:%p\\\\n'", "r");
	char line[8192];
	while (true)
	{
		fgets(line, 8192, f);
		if (feof(f))
			break;
		strchr(line, '\n')[0] = '\0';
		array<cstring> parts = cstring(line).csplit<5>(":");
		// parts: type, size, time, time, time, name
		struct statx stx;
		char type = parts[0][0];
		if (type == 'f')
			stx.stx_mode = S_IFREG;
		else if (type == 'd')
			stx.stx_mode = S_IFDIR;
		else if (type == 'l')
			stx.stx_mode = S_IFLNK;
		else
		{
			puts("find returned unknown type "+parts[0]+" for "+parts[5]);
			continue;
		}
		fromstring(parts[1], stx.stx_size);
		statx_parse_time(parts[2], &stx.stx_mtime);
		statx_parse_time(parts[3], &stx.stx_ctime);
		statx_parse_time(parts[4], &stx.stx_btime); // currently doesn't work (just returns -1.-000000010 for everything), but...
		
		string child_path = parts[5].substr(0, ~0);
		while (child_path.contains("//"))
			child_path = child_path.replace("//", "/");
		while (child_path.startswith("/"))
			child_path = child_path.substr(1, ~0);
		while (child_path.endswith("/"))
			child_path = child_path.substr(0, ~1);
		array<cstring> child_parts;
		if (child_path.contains("/"))
			child_parts = child_path.crsplit<1>("/");
		else
			child_parts = { "", child_path };
		array<dir_entry>& dir = statx_cache.get_create(child_parts[0]);
		dir.append({ child_parts[1], stx });
	}
	pclose(f);
}
static arrayview<dir_entry> statx_cache_get(cstring dirname)
{
	string key = dirname;
	if (key.startswith(statx_prefix))
		key = key.substr(statx_prefix.length(), ~0);
	else
	{
		puts("illegal argument to statx_cache_get");
		return nullptr;
	}
	while (key.contains("//"))
		key = key.replace("//", "/");
	while (key.startswith("/"))
		key = key.substr(1, ~0);
	while (key.endswith("/"))
		key = key.substr(0, ~1);
	array<dir_entry>* x = statx_cache.get_or_null(key);
	if (x)
		return *x;
	else
		return nullptr;
}

static bool sync_dir(group& g, cstring dir)
{
	struct statx_pair
	{
		struct statx stat1;
		struct statx stat2;
	};
	map<string, statx_pair> files;
	
	static const uint32_t stx_mask = STATX_TYPE|STATX_MTIME|STATX_CTIME|STATX_SIZE|STATX_BTIME;
	listdir(g.dir_a+dir, [&files](DIR* dirp, cstrnul name) {
		if (statx(dirfd(dirp), name, AT_SYMLINK_NOFOLLOW, stx_mask, &files.get_create(name).stat1) < 0)
		{
			perror("statx");
			puts(name);
			exit(1);
		}
	});
	arrayview<dir_entry> b_contents = statx_cache_get(g.dir_b+dir);
	if (b_contents)
	{
		for (const dir_entry& dirp : b_contents)
		{
			files.get_create(dirp.name).stat2 = dirp.stx;
		}
	}
	else
	{
		listdir(g.dir_b+dir, [&files, dir](DIR* dirp, cstrnul name) {
			if (statx(dirfd(dirp), name, AT_SYMLINK_NOFOLLOW, stx_mask, &files.get_create(name).stat2) < 0)
			{
				perror("statx");
				puts(name);
				exit(1);
			}
		});
	}
	
	if (files.contains("CACHEDIR.TAG"))
		return false;
	
	bool did_anything = false;
	for (auto& pair : files)
	{
		cfg.n_files_seen++;
		if (cfg.n_files_seen % 100 == 0)
		{
			printf("%zu/%zu\r", cfg.n_files_seen, cfg.n_files_total);
			fflush(stdout);
		}
		
		string name = dir+"/"+pair.key;
		if (g.is_ignored(name))
			continue;
		
		struct statx & stat1 = pair.value.stat1;
		struct statx & stat2 = pair.value.stat2;
		newer_t newer = is_newer(g, stat1, stat2);
		struct timespec new_time = { stat1.stx_mtime.tv_sec, stat1.stx_mtime.tv_nsec };
		if (newer & n_dir2)
			new_time = { stat2.stx_mtime.tv_sec, stat2.stx_mtime.tv_nsec };
		struct timespec new_times[2] = { new_time, new_time }; // have to set both ctime and mtime, ctime=UTIME_OMIT makes it ignore mtime
		if (newer == n_neither)
		{
		dont_know:
			puts("don't know what to do with "+name);
			continue;
		}
		if (newer == n_equal)
		{
			if (S_ISDIR(stat1.stx_mode))
			{
			sync_dir:
				bool child_did_anything = sync_dir(g, name);
				if (cfg_cli.dry_run) continue;
				if (child_did_anything || newer != n_equal)
				{
					utimensat(AT_FDCWD, g.dir_a+"/"+name, new_times, AT_SYMLINK_NOFOLLOW);
					utimensat(AT_FDCWD, g.dir_b+"/"+name, new_times, AT_SYMLINK_NOFOLLOW);
				}
			}
			continue;
		}
		struct statx& st_wrong = (newer == n_dir2 ? pair.value.stat1 : pair.value.stat2);
		struct statx& st_correct = (newer == n_dir2 ? pair.value.stat2 : pair.value.stat1);
		string fullpath_wrong  =  (newer == n_dir2 ? g.dir_a+"/"+name : g.dir_b+"/"+name);
		string fullpath_correct = (newer == n_dir2 ? g.dir_b+"/"+name : g.dir_a+"/"+name);
		if (st_correct.stx_mode == 0)
		{
			if (S_ISDIR(st_wrong.stx_mode))
			{
				puts("rmdir "+fullpath_wrong);
				if (cfg_cli.dry_run) continue;
				puts("todo");
			}
			else
			{
				puts("unlink "+fullpath_wrong);
				if (cfg_cli.dry_run) continue;
				puts("todo");
			}
			continue;
		}
		else if (S_ISDIR(st_correct.stx_mode))
		{
			if (st_wrong.stx_mode == 0)
			{
				puts("mkdir "+fullpath_wrong);
				if (cfg_cli.dry_run) continue;
				mkdir(fullpath_wrong, 0755);
				did_anything = true;
			}
			goto sync_dir;
		}
		else if (S_ISREG(st_correct.stx_mode))
		{
			puts("creat "+fullpath_wrong);
			if (cfg_cli.dry_run) continue;
			int fd_correct = open(fullpath_correct, O_RDONLY);
			int fd_wrong = open(fullpath_wrong, O_WRONLY|O_CREAT|O_TRUNC, 0644);
			size_t actual = sendfile(fd_wrong, fd_correct, nullptr, st_correct.stx_size);
			if (actual != st_correct.stx_size)
				puts("transfer error");
			futimens(fd_wrong, new_times);
			close(fd_correct);
			close(fd_wrong);
		}
		else if (S_ISLNK(st_correct.stx_mode))
		{
			puts("mksymlink "+fullpath_wrong);
			if (cfg_cli.dry_run) continue;
			char buf[1024];
			ssize_t buflen = readlink(fullpath_correct, buf, sizeof(buf)-1);
			if (buflen <= 0)
				goto dont_know;
			buf[buflen] = '\0';
			unlink(fullpath_wrong);
			symlink(buf, fullpath_wrong);
			utimensat(AT_FDCWD, fullpath_wrong, new_times, AT_SYMLINK_NOFOLLOW);
		}
		else
			goto dont_know;
	}
	return did_anything;
}

int main(int argc, char** argv)
{
	string cfgpath;
	
	argparse args;
	args.add("dry-run", &cfg_cli.dry_run);
	args.add("push", &cfg_cli.direction_push);
	args.add("pull", &cfg_cli.direction_pull);
	args.add("", &cfgpath);
	args.parse(argv);
	
	if (!cfgpath)
		cfgpath = file::exedir()+"elbow.json";
	cfg = jsondeserialize<config>(file::readallt(cfgpath));
	cfg.current_time = time(NULL);
	
	for (group& g : cfg.groups)
	{
		for (cstrnul dir : g.dirs)
		{
			if (dir[0] != '!')
			{
				puts(g.dir_a+dir);
				statx_cache_create(g.dir_b+dir, cfg.ssh[0], cfg.ssh[1]);
				sync_dir(g, dir);
			}
		}
	}
	if (!cfg_cli.dry_run)
		file::writeall(cfgpath, jsonserialize<2>(cfg));
	printf("\x1B[2K\r");
}
