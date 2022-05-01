#include "arlib.h"
#include <fcntl.h>
#include <sys/sendfile.h>

static bool dry_run;
static bool one_direction;
static time_t last_run;
static time_t current_time;

static size_t n_files_seen = 0;
static size_t n_files_total = 0;

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
	
	if (one_direction)
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
		bool do_delete = (file_time < min(current_time - 20*3600, last_run));
		if (do_delete)
			return stat1.stx_mode ? n_dir2 : n_dir1;
		else
			return stat1.stx_mode ? n_dir1 : n_dir2;
	}
	
	if (last_run >= 0 && last_run < stat1.stx_mtime.tv_sec && last_run < stat2.stx_mtime.tv_sec &&
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

static bool sync_dir(cstrnul dir1, cstrnul dir2)
{
	struct statx_pair
	{
		struct statx stat1;
		struct statx stat2;
	};
	map<string, statx_pair> files;
	
	static const uint32_t stx_mask = STATX_TYPE|STATX_MTIME|STATX_CTIME|STATX_SIZE|STATX_BTIME;
	listdir(dir1, [&files](DIR* dir, cstrnul name) {
		statx(dirfd(dir), name, AT_SYMLINK_NOFOLLOW, stx_mask, &files.get_create(name).stat1);
	});
	listdir(dir2, [&files](DIR* dir, cstrnul name) {
		statx(dirfd(dir), name, AT_SYMLINK_NOFOLLOW, stx_mask, &files.get_create(name).stat2);
	});
	
	if (files.contains("CACHEDIR.TAG"))
		return false;
	
	bool did_anything = false;
	for (auto& pair : files)
	{
		n_files_seen++;
		if (n_files_seen % 100 == 0)
		{
			printf("%zu/%zu\r", n_files_seen, n_files_total);
			fflush(stdout);
		}
		
		// annoying to sync, git likes chmodding them to 444
		if (pair.key == ".git")
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
			puts("don't know what to do with "+dir1+"/"+pair.key);
			continue;
		}
		if (newer == n_equal)
		{
			if (S_ISDIR(stat1.stx_mode))
			{
			sync_dir:
				bool child_did_anything = sync_dir(dir1+"/"+pair.key, dir2+"/"+pair.key);
				if (dry_run) continue;
				if (child_did_anything || newer != n_equal)
				{
					utimensat(AT_FDCWD, dir1+"/"+pair.key, new_times, AT_SYMLINK_NOFOLLOW);
					utimensat(AT_FDCWD, dir2+"/"+pair.key, new_times, AT_SYMLINK_NOFOLLOW);
				}
			}
			continue;
		}
		struct statx& st_wrong = (newer == n_dir2 ? pair.value.stat1 : pair.value.stat2);
		struct statx& st_correct = (newer == n_dir2 ? pair.value.stat2 : pair.value.stat1);
		cstrnul dir_wrong = (newer == n_dir2 ? dir1 : dir2);
		cstrnul dir_correct = (newer == n_dir2 ? dir2 : dir1);
		if (st_correct.stx_mode == 0)
		{
			if (S_ISDIR(st_wrong.stx_mode))
			{
				puts("rmdir "+dir_wrong+"/"+pair.key);
				if (dry_run) continue;
				puts("todo");
			}
			else
			{
				puts("unlink "+dir_wrong+"/"+pair.key);
				if (dry_run) continue;
				puts("todo");
			}
			continue;
		}
		else if (S_ISDIR(st_correct.stx_mode))
		{
			if (st_wrong.stx_mode == 0)
			{
				puts("mkdir "+dir_wrong+"/"+pair.key);
				if (dry_run) continue;
				mkdir(dir_wrong+"/"+pair.key, 0755);
				did_anything = true;
			}
			goto sync_dir;
		}
		else if (S_ISREG(st_correct.stx_mode))
		{
			puts("creat "+dir_wrong+"/"+pair.key);
			if (dry_run) continue;
			int fd_correct = open(dir_correct+"/"+pair.key, O_RDONLY);
			int fd_wrong = open(dir_wrong+"/"+pair.key, O_WRONLY|O_CREAT|O_TRUNC, 0644);
			size_t actual = sendfile(fd_wrong, fd_correct, nullptr, st_correct.stx_size);
			if (actual != st_correct.stx_size)
				puts("transfer error");
			futimens(fd_wrong, new_times);
			close(fd_correct);
			close(fd_wrong);
		}
		else if (S_ISLNK(st_correct.stx_mode))
		{
			puts("mksymlink "+dir_wrong+"/"+pair.key);
			if (dry_run) continue;
			char buf[1024];
			ssize_t buflen = readlink(dir_correct+"/"+pair.key, buf, sizeof(buf)-1);
			if (buflen <= 0)
				goto dont_know;
			buf[buflen] = '\0';
			unlink(dir_wrong+"/"+pair.key);
			symlink(buf, dir_wrong+"/"+pair.key);
			utimensat(AT_FDCWD, dir_wrong+"/"+pair.key, new_times, AT_SYMLINK_NOFOLLOW);
		}
		else
			goto dont_know;
	}
	return did_anything;
}

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

int main(int argc, char** argv)
{
	argparse args;
	args.add("dry-run", &dry_run);
	args.add("one-direction", &one_direction);
	args.parse(argv);
	
	current_time = time(NULL);
	string cfgpath = file::exedir()+"elbow.json";
	map<string,string> cfg = jsondeserialize<map<string,string>>(file::readallt(cfgpath));
	dry_run |= (bool)cfg.get_or("dry_run", "");
	one_direction |= (bool)cfg.get_or("one_direction", "");
	last_run = parse_time(cfg.get_or("last_run", ""));
	n_files_total = try_fromstring<size_t>(cfg.get_or("n_files", ""));
	
	for (auto pair : cfg)
	{
		if (!pair.key.startswith("/"))
			continue;
		sync_dir(pair.key, pair.value);
	}
	cfg.insert("last_run", serialize_time(time(NULL)+1)); // may be greater than current_time
	cfg.insert("n_files", tostring(n_files_seen));
	if (!dry_run)
		file::writeall(cfgpath, jsonserialize<2>(cfg));
	return 0;
}
