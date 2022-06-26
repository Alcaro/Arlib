#include "arlib.h"
#include <fcntl.h>
#include <sys/sendfile.h>

//static bool dry_run;
//static bool one_direction;
//static time_t last_run;
//static time_t current_time;

//static size_t n_files_seen = 0;
//static size_t n_files_total = 0;

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

struct config {
	time_t last_run;
	time_t current_time;
	size_t n_files_seen;
	size_t n_files_total;
	string dir_a;
	string dir_b;
	array<string> dirs;
	bool dry_run;
	bool one_direction;
	
	template<typename T>
	void serialize(T& s)
	{
		string last_run_s;
		if (s.serializing)
			last_run_s = serialize_time(current_time);
		s.items(
			"last_run", last_run_s,
			"n_files", (s.serializing ? n_files_seen : n_files_total),
			"a", dir_a,
			"b", dir_b,
			"dirs", dirs);
		if (!s.serializing)
			last_run = parse_time(last_run_s);
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

config cfg;

enum newer_t {
	n_neither = 0,
	n_dir1 = 1,
	n_dir2 = 2,
	n_equal = 3,
};

static newer_t is_newer(const struct statx & stat1, const struct statx & stat2)
{
	if (stat1.stx_mtime.tv_sec == stat2.stx_mtime.tv_sec)
		return n_equal;
	
	if (cfg.one_direction)
		return n_dir1;
	
	if (stat1.stx_mode == 0 || stat2.stx_mode == 0)
	{
		// if one file is gone, the other will be deleted only if
		// - mtime was at least 20 hours ago
		// - ctime was at least 20 hours ago (if not, it was probably renamed)
		// - btime was at least 20 hours ago (if not, it was probably downloaded and backdated)
		// - the above three are also prior to last sync
		const struct statx & st = (stat1.stx_mode ? stat1 : stat2);
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

static bool sync_dir(cstring dir)
{
	struct statx_pair
	{
		struct statx stat1;
		struct statx stat2;
	};
	map<string, statx_pair> files;
	
	static const uint32_t stx_mask = STATX_TYPE|STATX_MTIME|STATX_CTIME|STATX_SIZE|STATX_BTIME;
	listdir(cfg.dir_a+dir, [&files](DIR* dir, cstrnul name) {
		if (statx(dirfd(dir), name, AT_SYMLINK_NOFOLLOW, stx_mask, &files.get_create(name).stat1) < 0)
		{
			perror("statx");
			puts(name);
			exit(1);
		}
	});
	listdir(cfg.dir_b+dir, [&files](DIR* dir, cstrnul name) {
		if (statx(dirfd(dir), name, AT_SYMLINK_NOFOLLOW, stx_mask, &files.get_create(name).stat2) < 0)
		{
			perror("statx");
			puts(name);
			exit(1);
		}
	});
	
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
		if (cfg.is_ignored(name))
			continue;
		
		struct statx & stat1 = pair.value.stat1;
		struct statx & stat2 = pair.value.stat2;
		newer_t newer = is_newer(stat1, stat2);
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
				bool child_did_anything = sync_dir(name);
				if (cfg.dry_run) continue;
				if (child_did_anything || newer != n_equal)
				{
					utimensat(AT_FDCWD, cfg.dir_a+"/"+name, new_times, AT_SYMLINK_NOFOLLOW);
					utimensat(AT_FDCWD, cfg.dir_b+"/"+name, new_times, AT_SYMLINK_NOFOLLOW);
				}
			}
			continue;
		}
		struct statx& st_wrong = (newer == n_dir2 ? pair.value.stat1 : pair.value.stat2);
		struct statx& st_correct = (newer == n_dir2 ? pair.value.stat2 : pair.value.stat1);
		string fullpath_wrong  =  (newer == n_dir2 ? cfg.dir_a+"/"+name : cfg.dir_b+"/"+name);
		string fullpath_correct = (newer == n_dir2 ? cfg.dir_b+"/"+name : cfg.dir_a+"/"+name);
		if (st_correct.stx_mode == 0)
		{
			if (S_ISDIR(st_wrong.stx_mode))
			{
				puts("rmdir "+fullpath_wrong);
				if (cfg.dry_run) continue;
				puts("todo");
			}
			else
			{
				puts("unlink "+fullpath_wrong);
				if (cfg.dry_run) continue;
				puts("todo");
			}
			continue;
		}
		else if (S_ISDIR(st_correct.stx_mode))
		{
			if (st_wrong.stx_mode == 0)
			{
				puts("mkdir "+fullpath_wrong);
				if (cfg.dry_run) continue;
				mkdir(fullpath_wrong, 0755);
				did_anything = true;
			}
			goto sync_dir;
		}
		else if (S_ISREG(st_correct.stx_mode))
		{
			puts("creat "+fullpath_wrong);
			if (cfg.dry_run) continue;
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
			if (cfg.dry_run) continue;
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
	string cfgpath = file::exedir()+"elbow.json";
	cfg = jsondeserialize<config>(file::readallt(cfgpath));
	cfg.current_time = time(NULL);
	
	argparse args;
	args.add("dry-run", &cfg.dry_run);
	args.add("one-direction", &cfg.one_direction);
	args.parse(argv);
	
	for (cstrnul dir : cfg.dirs)
	{
		if (dir[0] != '!')
		{
			puts(dir);
			sync_dir(dir);
		}
	}
	if (!cfg.dry_run)
		file::writeall(cfgpath, jsonserialize<2>(cfg));
	printf("\x1B[2K\r");
}
