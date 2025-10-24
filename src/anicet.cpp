// anicet.cpp

#include <fcntl.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

// Encoder experiment runner
#include "anicet_runner.h"

// Android MediaCodec library for binder cleanup
#include "android_mediacodec_lib.h"


// small utilities
static long now_ms_monotonic() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static bool read_file(const std::string& path, std::string& out) {
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  char buf[8192];
  out.clear();
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return false;
    }
    if (n == 0) break;
    out.append(buf, buf + n);
  }
  close(fd);
  return true;
}

static long parse_vmhwm_kb_from_status(const std::string& status) {
  // Look for a line like: "VmHWM:\t  123456 kB"
  size_t pos = status.find("VmHWM:");
  if (pos == std::string::npos) return -1;
  pos += 6;
  // Skip spaces and tabs
  while (pos < status.size() && (status[pos] == ' ' || status[pos] == '\t'))
    ++pos;
  long kb = -1;
  // sscanf on a substring is fine; copy until end of line
  size_t e = status.find('\n', pos);
  std::string line = status.substr(
      pos, (e == std::string::npos) ? std::string::npos : e - pos);
  if (sscanf(line.c_str(), "%ld", &kb) == 1) return kb;
  return -1;
}

static bool set_affinity_from_cpulist(const std::string& cpus) {
#ifdef __linux__
  cpu_set_t set;
  CPU_ZERO(&set);
  // cpus format examples: "0", "1-3", "0,2,4-6"
  size_t i = 0, n = cpus.size();
  while (i < n) {
    // parse number
    char* endptr = nullptr;
    long a = strtol(cpus.c_str() + i, &endptr, 10);
    if (endptr == cpus.c_str() + i) return false;
    i = endptr - cpus.c_str();
    long b = a;
    if (i < n && cpus[i] == '-') {
      ++i;
      long r = strtol(cpus.c_str() + i, &endptr, 10);
      if (endptr == cpus.c_str() + i) return false;
      i = endptr - cpus.c_str();
      b = r;
    }
    if (a > b) std::swap(a, b);
    for (long c = a; c <= b; ++c) {
      CPU_SET((int)c, &set);
    }
    if (i < n && cpus[i] == ',') ++i;
  }
  return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
  (void)cpus;
  return false;
#endif
}

static void set_nice(int prio) {
  // prio: from -20 (high priority) to 19 (low). On Android non-root, range may
  // be limited.
  errno = 0;
  setpriority(PRIO_PROCESS, 0, prio);
}

// Parse simpleperf stat output
// Example line: "  1,234,567  cpu-cycles  # 1.234 GHz"
static std::map<std::string, long> parse_simpleperf_output(
    const std::string& output) {
  std::map<std::string, long> metrics;
  size_t pos = 0;
  while (pos < output.size()) {
    size_t eol = output.find('\n', pos);
    if (eol == std::string::npos) eol = output.size();
    std::string line = output.substr(pos, eol - pos);
    pos = eol + 1;

    // Skip empty lines and headers
    if (line.empty() || line.find("Performance counter") != std::string::npos ||
        line.find("Total test time") != std::string::npos)
      continue;

    // Parse lines like: "  1,234,567  cpu-cycles"
    // Remove leading spaces
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
      ++start;
    if (start >= line.size()) continue;

    // Find first space after the number
    size_t num_end = start;
    long value = 0;
    std::string num_str;
    while (num_end < line.size() &&
           (isdigit(line[num_end]) || line[num_end] == ',' ||
            line[num_end] == '.')) {
      if (line[num_end] != ',') num_str += line[num_end];
      ++num_end;
    }
    if (!num_str.empty()) value = atol(num_str.c_str());

    // Find event name
    while (num_end < line.size() &&
           (line[num_end] == ' ' || line[num_end] == '\t'))
      ++num_end;
    if (num_end >= line.size()) continue;

    size_t name_end = num_end;
    while (name_end < line.size() && line[name_end] != ' ' &&
           line[name_end] != '\t' && line[name_end] != '#')
      ++name_end;

    std::string event = line.substr(num_end, name_end - num_end);
    if (!event.empty() && value >= 0) {
      // Replace hyphens with underscores for consistent naming
      for (char& c : event) {
        if (c == '-') c = '_';
      }
      metrics[event] = value;
    }
  }
  return metrics;
}


// CLI parsing
struct Options {
  std::vector<std::string> cmd;
  std::vector<std::pair<std::string, std::string>> tags;
  std::string cpus;                     // e.g. "4-7"
  int nice = 0;                         // 0 means unchanged
  int timeout_ms = 0;                   // 0 means no timeout
  bool json = false;                    // default CSV
  bool use_simpleperf = false;          // wrap with simpleperf (default false)
  std::string simpleperf_events;        // comma-separated event list
  // Media input parameters for library API mode
  std::string image_file;
  int width = 0;
  int height = 0;
  std::string color_format;
  std::string codec = "all";            // codec to use (default: all)
  int num_runs = 1;                     // number of encoding runs (default: 1)
};

static void print_help(const char* argv0) {
  fprintf(
      stderr,
      "Usage:\n"
      "  %s [options] -- <command> [args...]\n"
      "  %s [options] --image FILE --width N --height N --color-format FORMAT\n\n"
      "Options:\n"
      "  --tag key=val            Repeatable; attach metadata to output row\n"
      "  --cpus LIST              CPU affinity, e.g. 0,2,4-5\n"
      "  --nice N                 Set niceness [-20..19]; requires privileges "
      "for negative\n"
      "  --timeout-ms N           Kill child if it runs longer than N ms\n"
      "  --json                   Emit JSON (default: CSV)\n"
      "  --simpleperf             Wrap with simpleperf (default: disabled)\n"
      "  --no-simpleperf          Disable simpleperf wrapping\n"
      "  --simpleperf-events LIST Comma-separated perf events\n"
      "  --image FILE             Image file to encode (library API mode)\n"
      "  --width N                Image width in pixels\n"
      "  --height N               Image height in pixels\n"
      "  --color-format FORMAT    Color format (e.g., yuv420p)\n"
      "  --codec CODEC            Codec to use: x265, x265-nonopt, svt-av1,\n"
      "                           libjpeg-turbo, libjpeg-turbo-nonopt, jpegli,\n"
      "                           webp, webp-nonopt, mediacodec, all (default: all)\n"
      "  --num-runs N             Number of encoding runs for profiling (default: 1)\n"
      "  -h, --help               Show help\n\n"
      "Outputs fields:\n"
      "  wall_ms,user_ms,sys_ms,vmhwm_kb,exit[,simpleperf metrics...]\n",
      argv0, argv0);
}

static bool starts_with(const char* s, const char* pfx) {
  return strncmp(s, pfx, strlen(pfx)) == 0;
}

static bool parse_cli(int argc, char** argv, Options& opt) {
  int i = 1;
  for (; i < argc; ++i) {
    if (strcmp(argv[i], "--") == 0) {
      ++i;
      break;
    }
    if (strcmp(argv[i], "--json") == 0) {
      opt.json = true;
      continue;
    }
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_help(argv[0]);
      exit(0);
    }
    if (starts_with(argv[i], "--tag")) {
      std::string kv;
      if (strcmp(argv[i], "--tag") == 0) {
        if (i + 1 >= argc) {
          fprintf(stderr, "--tag needs key=val\n");
          return false;
        }
        kv = argv[++i];
      } else {
        const char* eq = strchr(argv[i], '=');
        if (!eq) {
          fprintf(stderr, "--tag needs key=val\n");
          return false;
        }
        kv = eq + 1;
      }
      size_t p = kv.find('=');
      if (p == std::string::npos) {
        fprintf(stderr, "--tag needs key=val\n");
        return false;
      }
      opt.tags.emplace_back(kv.substr(0, p), kv.substr(p + 1));
      continue;
    }
    if (strcmp(argv[i], "--cpus") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--cpus needs a list\n");
        return false;
      }
      opt.cpus = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--nice") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--nice needs N\n");
        return false;
      }
      opt.nice = atoi(argv[++i]);
      continue;
    }
    if (strcmp(argv[i], "--timeout-ms") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--timeout-ms needs N\n");
        return false;
      }
      opt.timeout_ms = atoi(argv[++i]);
      continue;
    }
    if (strcmp(argv[i], "--simpleperf") == 0) {
      opt.use_simpleperf = true;
      continue;
    }
    if (strcmp(argv[i], "--no-simpleperf") == 0) {
      opt.use_simpleperf = false;
      continue;
    }
    if (strcmp(argv[i], "--simpleperf-events") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--simpleperf-events needs event list\n");
        return false;
      }
      opt.simpleperf_events = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--image") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--image needs a file path\n");
        return false;
      }
      opt.image_file = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--width") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--width needs N\n");
        return false;
      }
      opt.width = atoi(argv[++i]);
      continue;
    }
    if (strcmp(argv[i], "--height") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--height needs N\n");
        return false;
      }
      opt.height = atoi(argv[++i]);
      continue;
    }
    if (strcmp(argv[i], "--color-format") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--color-format needs a format\n");
        return false;
      }
      opt.color_format = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "--codec") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--codec needs a codec name\n");
        return false;
      }
      opt.codec = argv[++i];
      // Validate codec name
      std::set<std::string> valid_codecs = {
        "x265", "x265-nonopt", "svt-av1", "libjpeg-turbo",
        "libjpeg-turbo-nonopt", "jpegli", "webp", "webp-nonopt",
        "mediacodec", "all"
      };
      if (valid_codecs.find(opt.codec) == valid_codecs.end()) {
        fprintf(stderr, "Invalid codec: %s\n", opt.codec.c_str());
        return false;
      }
      continue;
    }
    if (strcmp(argv[i], "--num-runs") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--num-runs needs a number\n");
        return false;
      }
      opt.num_runs = atoi(argv[++i]);
      if (opt.num_runs < 1) {
        fprintf(stderr, "--num-runs must be >= 1\n");
        return false;
      }
      continue;
    }
    fprintf(stderr, "Unknown option: %s\n", argv[i]);
    return false;
  }

  // Command is optional if media parameters are provided
  bool has_media_params = !opt.image_file.empty() && opt.width > 0 &&
                          opt.height > 0 && !opt.color_format.empty();

  if (i >= argc) {
    // No command provided - check if we have media parameters
    if (!has_media_params) {
      fprintf(stderr, "Missing -- and command, or --image/--width/--height/--color-format\n");
      return false;
    }
    return true;
  }

  // Command provided - collect it
  for (; i < argc; ++i) {
    opt.cmd.emplace_back(argv[i]);
  }

  // Can't have both command and media parameters
  if (has_media_params && !opt.cmd.empty()) {
    fprintf(stderr, "Cannot specify both command and media parameters\n");
    return false;
  }

  return !opt.cmd.empty() || has_media_params;
}


// global for signal forwarding
static pid_t g_child = -1;
static void relay_signal(int sig) {
  if (g_child > 0) kill(g_child, sig);
}

static void install_signal_handlers() {
  struct sigaction sa{};
  sa.sa_handler = relay_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  int sigs[] = {SIGINT, SIGTERM, SIGHUP};
  for (int s : sigs) {
    sigaction(s, &sa, nullptr);
  }
}


int main(int argc, char** argv) {
  Options opt;
  if (!parse_cli(argc, argv, opt)) {
    print_help(argv[0]);
    return 2;
  }

  install_signal_handlers();

  long t0_ms = now_ms_monotonic();

  // Check if we're in library API mode (media parameters provided)
  bool library_mode = !opt.image_file.empty() && opt.width > 0 &&
                      opt.height > 0 && !opt.color_format.empty();

  // Create temp file for simpleperf output if needed
  std::string simpleperf_out_path;
  if (opt.use_simpleperf) {
    char tmpl[] = "/data/local/tmp/simpleperf_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
      perror("mkstemp");
      return 2;
    }
    close(fd);
    simpleperf_out_path = tmpl;
  }

  // Library API mode: call anicet_experiment() directly
  if (library_mode && !opt.use_simpleperf) {
    // Apply affinity and nice settings
    if (!opt.cpus.empty()) set_affinity_from_cpulist(opt.cpus);
    if (opt.nice != 0) set_nice(opt.nice);

    // Read image file
    std::string image_data;
    if (!read_file(opt.image_file, image_data)) {
      fprintf(stderr, "Failed to read image file: %s\n", opt.image_file.c_str());
      return 1;
    }

    // Call anicet_experiment()
    int result = anicet_experiment(
        reinterpret_cast<const uint8_t*>(image_data.data()),
        image_data.size(),
        opt.height,
        opt.width,
        opt.color_format.c_str(),
        opt.codec.c_str(),
        opt.num_runs
    );

    long t1_ms = now_ms_monotonic();
    long wall_ms = t1_ms - t0_ms;

    // Output results
    if (opt.json) {
      printf("{");
      bool first = true;
      auto emit_ki = [&](const char* k, long v) {
        printf("%s\"%s\":%ld", first ? "" : ",", k, v);
        first = false;
      };
      for (auto& kv : opt.tags) {
        printf("%s\"%s\":\"%s\"", first ? "" : ",", kv.first.c_str(), kv.second.c_str());
        first = false;
      }
      emit_ki("wall_ms", wall_ms);
      emit_ki("exit", result);
      printf("}\n");
    } else {
      bool first = true;
      for (auto& kv : opt.tags) {
        printf("%s%s=%s", first ? "" : ",", kv.first.c_str(), kv.second.c_str());
        first = false;
      }
      if (first) printf("run=na");
      printf(",wall_ms=%ld,exit=%d\n", wall_ms, result);
    }

    // Flush pending binder commands to ensure clean shutdown
    // This ensures all MediaCodec cleanup commands are sent to the media server
    // before the process exits, reducing the chance of leaving the media server
    // in a bad state that could affect the next process invocation.
    android_mediacodec_flush_binder();

    // NOTE: We do NOT call android_mediacodec_cleanup_binder() here.
    // Stopping the binder thread can cause race conditions. Instead, we just
    // flush commands and let the OS clean up the thread when the process exits.

    return result;
  }

  // Fork
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 2;
  }

  if (pid == 0) {
    // Child: apply affinity and nice, then exec
    if (!opt.cpus.empty()) set_affinity_from_cpulist(opt.cpus);
    if (opt.nice != 0) set_nice(opt.nice);

    // Build argv for execvp
    std::vector<std::string> cmd_vec;
    std::vector<char*> av;

    if (opt.use_simpleperf) {
      // Wrap with simpleperf stat
      cmd_vec.push_back("simpleperf");
      cmd_vec.push_back("stat");
      if (!opt.simpleperf_events.empty()) {
        cmd_vec.push_back("-e");
        cmd_vec.push_back(opt.simpleperf_events);
      }
      cmd_vec.push_back("-o");
      cmd_vec.push_back(simpleperf_out_path);
      cmd_vec.push_back("--");

      if (library_mode) {
        // Re-exec ourselves with --no-simpleperf and media parameters
        cmd_vec.push_back(argv[0]);
        cmd_vec.push_back("--no-simpleperf");
        cmd_vec.push_back("--image");
        cmd_vec.push_back(opt.image_file);
        cmd_vec.push_back("--width");
        cmd_vec.push_back(std::to_string(opt.width));
        cmd_vec.push_back("--height");
        cmd_vec.push_back(std::to_string(opt.height));
        cmd_vec.push_back("--color-format");
        cmd_vec.push_back(opt.color_format);
        cmd_vec.push_back("--codec");
        cmd_vec.push_back(opt.codec);
      } else {
        // Wrap original command
        for (const auto& s : opt.cmd) {
          cmd_vec.push_back(s);
        }
      }
    } else {
      // No simpleperf wrapping
      cmd_vec = opt.cmd;
    }

    av.reserve(cmd_vec.size() + 1);
    for (auto& s : cmd_vec) {
      av.push_back(const_cast<char*>(s.c_str()));
    }
    av.push_back(nullptr);

    execvp(av[0], av.data());
    perror("execvp");
    _exit(127);
  }

  g_child = pid;

  // Optional timeout
  bool timed_out = false;
#ifdef __linux__
  if (opt.timeout_ms > 0) {
    // Use setitimer to keep it simple and portable
    struct itimerval it{};
    it.it_value.tv_sec = opt.timeout_ms / 1000;
    it.it_value.tv_usec = (opt.timeout_ms % 1000) * 1000;
    setitimer(ITIMER_REAL, &it, nullptr);
    // SIGALRM will arrive to parent; handle inline in wait loop.
  }
#endif

  // Wait for child exit but do not reap yet
  siginfo_t si{};
  for (;;) {
    if (waitid(P_PID, pid, &si, WEXITED | WNOWAIT) == 0) break;
    if (errno == EINTR) {
      // Check for timeout via elapsed time if setitimer is not desired
      if (opt.timeout_ms > 0) {
        long now = now_ms_monotonic();
        if (now - t0_ms > opt.timeout_ms) {
          timed_out = true;
          kill(pid, SIGKILL);
        }
      }
      continue;
    }
    perror("waitid");
    kill(pid, SIGKILL);
    break;
  }

  // Read VmHWM from /proc/<pid>/status while child still exists
  long vmhwm_kb = -1;
  {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    std::string status;
    if (read_file(path, status)) vmhwm_kb = parse_vmhwm_kb_from_status(status);
  }

  // Now reap and get rusage
  int status_code = 0;
  struct rusage ru{};
  if (wait4(pid, &status_code, 0, &ru) < 0) {
    perror("wait4");
    return 2;
  }
  long t1_ms = now_ms_monotonic();

  long wall_ms = t1_ms - t0_ms;
  long user_ms = ru.ru_utime.tv_sec * 1000L + ru.ru_utime.tv_usec / 1000L;
  long sys_ms = ru.ru_stime.tv_sec * 1000L + ru.ru_stime.tv_usec / 1000L;

  int exit_code = -1;
  if (WIFEXITED(status_code))
    exit_code = WEXITSTATUS(status_code);
  else if (WIFSIGNALED(status_code))
    exit_code = 128 + WTERMSIG(status_code);

  if (timed_out && exit_code == -1) exit_code = 137;  // Killed

  // Parse simpleperf output if available
  std::map<std::string, long> simpleperf_metrics;
  if (opt.use_simpleperf && !simpleperf_out_path.empty()) {
    std::string simpleperf_output;
    if (read_file(simpleperf_out_path, simpleperf_output)) {
      simpleperf_metrics = parse_simpleperf_output(simpleperf_output);
    }
    // Clean up temp file
    unlink(simpleperf_out_path.c_str());
  }

  // Emit CSV or JSON
  if (opt.json) {
    // Minimal JSON, flat object with tags inline
    printf("{");
    bool first = true;
    auto emit_kv = [&](const char* k, const std::string& v) {
      printf("%s\"%s\":\"", first ? "" : ",", k);
      for (char c : v) {
        if (c == '"' || c == '\\') putchar('\\');
        putchar(c);
      }
      printf("\"");
      first = false;
    };
    auto emit_ki = [&](const char* k, long v) {
      printf("%s\"%s\":%ld", first ? "" : ",", k, v);
      first = false;
    };
    for (auto& kv : opt.tags) {
      emit_kv(kv.first.c_str(), kv.second);
    }
    emit_ki("wall_ms", wall_ms);
    emit_ki("user_ms", user_ms);
    emit_ki("sys_ms", sys_ms);
    emit_ki("vmhwm_kb", vmhwm_kb);
    emit_ki("exit", exit_code);
    // Add simpleperf metrics
    for (const auto& metric : simpleperf_metrics) {
      emit_ki(metric.first.c_str(), metric.second);
    }
    printf("}\n");
  } else {
    // CSV header is not printed; print one row with tags then metrics as
    // key=val pairs Easy to post-process with awk/sed or a small parser.
    bool first = true;
    for (auto& kv : opt.tags) {
      printf("%s%s=%s", first ? "" : ",", kv.first.c_str(), kv.second.c_str());
      first = false;
    }
    if (first) printf("run=na");  // no tags
    printf(",wall_ms=%ld,user_ms=%ld,sys_ms=%ld,vmhwm_kb=%ld,exit=%d", wall_ms,
           user_ms, sys_ms, vmhwm_kb, exit_code);
    // Add simpleperf metrics
    for (const auto& metric : simpleperf_metrics) {
      printf(",%s=%ld", metric.first.c_str(), metric.second);
    }
    printf("\n");
  }

  return exit_code;
}
