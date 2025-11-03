// anicet.cpp

#include <fcntl.h>
#include <getopt.h>
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
#include "anicet_runner_jpegli.h"

// Parameter descriptor system
#include "anicet_parameter.h"

// Codec-specific runners
#include "anicet_runner_libjpegturbo.h"
#include "anicet_runner_svtav1.h"
#include "anicet_runner_x265.h"
#include "anicet_runner_webp.h"

// Android MediaCodec library for binder cleanup
#include "android_mediacodec_lib.h"

// Version information
#include "anicet_version.h"

// JSON library
#include <nlohmann/json.hpp>


// Valid codec name(s)
std::set<std::string> VALID_CODECS = {
  "x265", "svt-av1", "libjpeg-turbo",
  "jpegli", "webp",
  "mediacodec", "all"
};

// small utilities
static long now_ms_monotonic() {
  struct timespec ts{};
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

// Get directory of the executable
static std::string get_executable_dir() {
  char path[4096];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len == -1) {
    // Fallback to current directory if readlink fails
    return ".";
  }
  path[len] = '\0';

  // Find last '/' to get directory
  std::string exe_path(path);
  size_t last_slash = exe_path.find_last_of('/');
  if (last_slash == std::string::npos) {
    return ".";
  }
  return exe_path.substr(0, last_slash);
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
  if (pos == std::string::npos) {
    return -1;
  }
  pos += 6;
  // Skip spaces and tabs
  while (pos < status.size() && (status[pos] == ' ' || status[pos] == '\t')) {
    ++pos;
  }
  long kb = -1;
  // sscanf on a substring is fine; copy until end of line
  size_t e = status.find('\n', pos);
  std::string line = status.substr(
      pos, (e == std::string::npos) ? std::string::npos : e - pos);
  if (sscanf(line.c_str(), "%ld", &kb) == 1) {
    return kb;
  }
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
      if (endptr == cpus.c_str() + i) {
        return false;
      }
      i = endptr - cpus.c_str();
      b = r;
    }
    if (a > b) {
      std::swap(a, b);
    }
    for (long c = a; c <= b; ++c) {
      CPU_SET((int)c, &set);
    }
    if (i < n && cpus[i] == ',') {
      ++i;
    }
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
    if (eol == std::string::npos) {
      eol = output.size();
    }
    std::string line = output.substr(pos, eol - pos);
    pos = eol + 1;

    // Skip empty lines and headers
    if (line.empty() || line.find("Performance counter") != std::string::npos ||
        line.find("Total test time") != std::string::npos) {
      continue;
    }

    // Parse lines like: "  1,234,567  cpu-cycles"
    // Remove leading spaces
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
      ++start;
    }
    if (start >= line.size()) {
      continue;
    }

    // Find first space after the number
    size_t num_end = start;
    long value = 0;
    std::string num_str;
    while (num_end < line.size() &&
           (isdigit(line[num_end]) || line[num_end] == ',' ||
            line[num_end] == '.')) {
      if (line[num_end] != ',') {
        num_str += line[num_end];
      }
      ++num_end;
    }
    if (!num_str.empty()) {
      value = atol(num_str.c_str());
    }

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


// Get device serial number (from environment or system property)
static std::string get_device_serial() {
  // Try ANDROID_SERIAL environment variable first (set by adb)
  const char* env_serial = getenv("ANDROID_SERIAL");
  if (env_serial && env_serial[0] != '\0') {
    return std::string(env_serial);
  }

#ifdef __ANDROID__
  // Try ro.serialno system property on Android
  FILE* pipe = popen("/system/bin/getprop ro.serialno 2>/dev/null", "r");
  if (pipe) {
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string serial(buffer);
      // Remove trailing newline
      if (!serial.empty() && serial.back() == '\n') {
        serial.pop_back();
      }
      pclose(pipe);
      if (!serial.empty()) {
        return serial;
      }
    }
    pclose(pipe);
  }
#endif

  // Return "unknown" if no serial number is available
  return "unknown";
}

// Helper to get parameter order for sorting
static int get_param_order(
    const std::string& param_name, const std::string& codec_name) {
  if (codec_name == "x265") {
    auto it = anicet::runner::x265::X265_PARAMETERS.find(param_name);
    if (it != anicet::runner::x265::X265_PARAMETERS.end()) {
      return it->second.order;
    }
  } else if (codec_name == "webp") {
    auto it = anicet::runner::webp::WEBP_PARAMETERS.find(param_name);
    if (it != anicet::runner::webp::WEBP_PARAMETERS.end()) {
      return it->second.order;
    }
  } else if (codec_name == "libjpeg-turbo") {
    auto it = anicet::runner::libjpegturbo::LIBJPEGTURBO_PARAMETERS.find(param_name);
    if (it != anicet::runner::libjpegturbo::LIBJPEGTURBO_PARAMETERS.end()) {
      return it->second.order;
    }
  } else if (codec_name == "svt-av1") {
    auto it = anicet::runner::svtav1::SVTAV1_PARAMETERS.find(param_name);
    if (it != anicet::runner::svtav1::SVTAV1_PARAMETERS.end()) {
      return it->second.order;
    }
  } else if (codec_name == "jpegli") {
    auto it = anicet::runner::jpegli::JPEGLI_PARAMETERS.find(param_name);
    if (it != anicet::runner::jpegli::JPEGLI_PARAMETERS.end()) {
      return it->second.order;
    }
  }
  // Default order for unknown parameters
  return 100;
}

// Get sorted parameters for consistent output ordering
static std::vector<std::pair<std::string, std::string>> get_sorted_params(
    const std::map<std::string, std::string>& params,
    const std::string& codec_name) {
  std::vector<std::pair<std::string, std::string>> sorted_params(
      params.begin(), params.end());

  std::sort(sorted_params.begin(), sorted_params.end(),
            [&codec_name](const std::pair<std::string, std::string>& a,
                         const std::pair<std::string, std::string>& b) {
              int order_a = get_param_order(a.first, codec_name);
              int order_b = get_param_order(b.first, codec_name);
              if (order_a != order_b) {
                return order_a < order_b;
              }
              return a.first < b.first;  // Alphabetical for same order
            });

  return sorted_params;
}

// Default debug level
#define DEFAULT_DEBUG_LEVEL 0

// CLI parsing
struct Options {
  std::vector<std::string> cmd;
  std::vector<std::pair<std::string, std::string>> tags;
  // e.g. "4-7"
  std::string cpus;
  // 0 means unchanged
  int nice = 0;
  // 0 means no timeout
  int timeout_ms = 0;
  // default CSV
  bool json = false;
  // wrap with simpleperf (default false)
  bool use_simpleperf = false;
  // comma-separated event list
  std::string simpleperf_events;
  // Media input parameters for library API mode
  std::string image_file;
  int width = 0;
  int height = 0;
  std::string color_format;
  // codec to use (default: all)
  std::string codec = "all";
  // number of encoding runs (default: 1)
  int num_runs = 1;
  // dump output files to disk (default: false)
  bool dump_output = false;
  // directory for output files (default: exe dir)
  std::string dump_output_dir;
  // prefix for output files (default: anicet.output)
  std::string dump_output_prefix;
  // debug level (0 = no debug, higher = more verbose)
  int debug = DEFAULT_DEBUG_LEVEL;
  // output file for JSON results (default: stdout)
  std::string output_file = "-";
  // device serial number (populated automatically)
  std::string serial_number;
  // x265 parameters from CLI (accumulated from multiple --x265 flags)
  std::vector<std::string> x265_params;
  // webp parameters from CLI (accumulated from multiple --webp flags)
  std::vector<std::string> webp_params;
  // libjpeg-turbo parameters from CLI (accumulated from multiple --libjpeg-turbo flags)
  std::vector<std::string> libjpegturbo_params;
  // svt-av1 parameters from CLI (accumulated from multiple --svt-av1 flags)
  std::vector<std::string> svtav1_params;
  // jpegli parameters from CLI (accumulated from multiple --jpegli flags)
  std::vector<std::string> jpegli_params;
  // parsed codec setup (populated after CLI parsing)
  CodecSetup codec_setup;
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
      "  --codec CODEC            Codec to use: x265, svt-av1,\n"
      "                           libjpeg-turbo, jpegli, webp,\n"
      "                           mediacodec, all (default: all)\n"
      "  --x265 PARAMS            x265 encoder parameters (repeatable, colon/comma-separated)\n"
      "                           Format: param=value:param=value or param=value,param=value\n"
      "                           Use '--x265 help' for parameter list\n"
      "  --webp PARAMS            webp encoder parameters (repeatable, colon/comma-separated)\n"
      "                           Format: param=value:param=value or param=value,param=value\n"
      "                           Use '--webp help' for parameter list\n"
      "  --libjpeg-turbo PARAMS   libjpeg-turbo encoder parameters (repeatable, colon/comma-separated)\n"
      "                           Format: param=value:param=value or param=value,param=value\n"
      "                           Use '--libjpeg-turbo help' for parameter list\n"
      "  --svt-av1 PARAMS         svt-av1 encoder parameters (repeatable, colon/comma-separated)\n"
      "                           Format: param=value:param=value or param=value,param=value\n"
      "                           Use '--svt-av1 help' for parameter list\n"
      "  --jpegli PARAMS          jpegli encoder parameters (repeatable, colon/comma-separated)\n"
      "                           Format: param=value:param=value or param=value,param=value\n"
      "                           Use '--jpegli help' for parameter list\n"
      "  --num-runs N             Number of encoding runs for profiling (default: 1)\n"
      "  --dump-output            Write output files to disk (default: disabled)\n"
      "  --no-dump-output         Do not write output files to disk\n"
      "  --dump-output-dir DIR    Directory for output files (default: exe directory)\n"
      "  --dump-output-prefix PFX Prefix for output files (default: anicet.output)\n"
      "  -o, --output FILE        Output file for JSON results (default: stdout, use '-' for stdout)\n"
      "  -d, --debug              Increase debug verbosity (can be repeated: -d -d or -dd)\n"
      "  --quiet                  Disable all debug output (sets debug level to 0)\n"
      "  --version                Show version information\n"
      "  -h, --help               Show help\n\n"
      "Outputs fields:\n"
      "  wall_ms,user_ms,sys_ms,vmhwm_kb,exit[,simpleperf metrics...]\n",
      argv0, argv0);
}

static bool parse_cli(int argc, char** argv, Options& opt) {
  // Define long options
  static struct option long_options[] = {
    {"help", no_argument, nullptr, 'h'},
    {"version", no_argument, nullptr, 'v'},
    {"json", no_argument, nullptr, 'j'},
    {"tag", required_argument, nullptr, 't'},
    {"cpus", required_argument, nullptr, 'c'},
    {"nice", required_argument, nullptr, 'n'},
    {"timeout-ms", required_argument, nullptr, 'T'},
    {"simpleperf", no_argument, nullptr, 's'},
    {"no-simpleperf", no_argument, nullptr, 'S'},
    {"simpleperf-events", required_argument, nullptr, 'e'},
    {"image", required_argument, nullptr, 'i'},
    {"width", required_argument, nullptr, 'w'},
    {"height", required_argument, nullptr, 'H'},
    {"color-format", required_argument, nullptr, 'f'},
    {"codec", required_argument, nullptr, 'C'},
    {"x265", required_argument, nullptr, 1000},
    {"webp", required_argument, nullptr, 1001},
    {"libjpeg-turbo", required_argument, nullptr, 1002},
    {"svt-av1", required_argument, nullptr, 1003},
    {"jpegli", required_argument, nullptr, 1004},
    {"num-runs", required_argument, nullptr, 'N'},
    {"dump-output", no_argument, nullptr, 'D'},
    {"no-dump-output", no_argument, nullptr, 'O'},
    {"dump-output-dir", required_argument, nullptr, 'r'},
    {"dump-output-prefix", required_argument, nullptr, 'p'},
    {"output", required_argument, nullptr, 'o'},
    {"debug", no_argument, nullptr, 'd'},
    {"quiet", no_argument, nullptr, 'q'},
    {nullptr, 0, nullptr, 0}
  };

  // Preprocess argv to expand -dd, -ddd into -d -d -d
  // First, build all expanded strings to avoid pointer invalidation
  std::vector<std::string> expanded_args;
  std::vector<int> arg_mapping; // Maps to original argv index, or -1 for expanded

  for (int i = 1; i < argc; i++) {
    // Check for -dd, -ddd, etc.
    if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][2] == 'd') {
      // Count consecutive 'd' characters
      int d_count = 0;
      for (size_t j = 1; argv[i][j] == 'd'; j++) {
        d_count++;
      }
      // Check if it's only 'd' characters (valid -dd format)
      if (argv[i][1 + d_count] == '\0') {
        // Expand -dd into multiple -d
        for (int j = 0; j < d_count; j++) {
          expanded_args.push_back("-d");
          arg_mapping.push_back(-1);
        }
        continue;
      }
    }
    arg_mapping.push_back(i);
  }

  // Now build new_argv with stable pointers
  std::vector<char*> new_argv;
  new_argv.push_back(argv[0]);

  int expanded_idx = 0;
  for (int mapping : arg_mapping) {
    if (mapping == -1) {
      // This is an expanded -d
      new_argv.push_back(const_cast<char*>(expanded_args[expanded_idx].c_str()));
      expanded_idx++;
    } else {
      // This is an original argument
      new_argv.push_back(argv[mapping]);
    }
  }

  int new_argc = new_argv.size();
  char** new_argv_ptr = new_argv.data();

  // Debug: print expanded argv
  if (getenv("ANICET_DEBUG_GETOPT")) {
    fprintf(stderr, "DEBUG: Expanded argv (new_argc=%d):\n", new_argc);
    for (int i = 0; i < new_argc; ++i) {
      fprintf(stderr, "  [%d] '%s'\n", i, new_argv_ptr[i]);
    }
  }

  // Reset getopt state for the new argv
  optind = 1;
  opterr = 1;

  // Ensure getopt uses permutation mode (allows options after non-options)
  // This is needed on some systems where POSIXLY_CORRECT might be set
  unsetenv("POSIXLY_CORRECT");

  // Parse options using getopt_long
  int opt_char;
  int option_index = 0;

  while ((opt_char = getopt_long(new_argc, new_argv_ptr, "hvjt:c:n:T:sSe:i:w:H:f:C:N:DOr:p:o:dq", long_options, &option_index)) != -1) {
    switch (opt_char) {
      case 'h':
        print_help(argv[0]);
        exit(0);

      case 'v':
        printf("anicet version %s\n", ANICET_VERSION);
        exit(0);

      case 'j':
        opt.json = true;
        break;

      case 't': {
        // Parse tag key=val
        std::string kv(optarg);
        size_t p = kv.find('=');
        if (p == std::string::npos) {
          fprintf(stderr, "--tag needs key=val\n");
          return false;
        }
        opt.tags.emplace_back(kv.substr(0, p), kv.substr(p + 1));
        break;
      }

      case 'c':
        opt.cpus = optarg;
        break;

      case 'n':
        opt.nice = atoi(optarg);
        break;

      case 'T':
        opt.timeout_ms = atoi(optarg);
        break;

      case 's':
        opt.use_simpleperf = true;
        break;

      case 'S':
        opt.use_simpleperf = false;
        break;

      case 'e':
        opt.simpleperf_events = optarg;
        break;

      case 'i':
        opt.image_file = optarg;
        break;

      case 'w':
        opt.width = atoi(optarg);
        break;

      case 'H':
        opt.height = atoi(optarg);
        break;

      case 'f':
        opt.color_format = optarg;
        break;

      case 'C': {
        opt.codec = optarg;
        // Split by comma and validate each codec
        std::string codec_list = opt.codec;
        size_t pos = 0;
        bool all_valid = true;
        while (pos < codec_list.length()) {
          size_t comma = codec_list.find(',', pos);
          if (comma == std::string::npos) {
            comma = codec_list.length();
          }
          // Extract codec name (trim spaces)
          size_t start = pos;
          while (start < comma && codec_list[start] == ' ') start++;
          size_t end = comma;
          while (end > start && codec_list[end - 1] == ' ') end--;

          std::string codec = codec_list.substr(start, end - start);
          if (VALID_CODECS.find(codec) == VALID_CODECS.end()) {
            fprintf(stderr, "Invalid codec: %s\n", codec.c_str());
            all_valid = false;
            break;
          }
          pos = comma + 1;
        }
        if (!all_valid) {
          return false;
        }
        break;
      }

      case 1000: {
        // Handle --x265 option
        std::string arg(optarg);

        // Check for help requests
        if (arg == "help" || arg == "help -q" || arg == "help -v") {
          using namespace anicet::parameter;
          HelpVerbosity verbosity = HelpVerbosity::CONCISE;

          if (arg == "help -q") {
            verbosity = HelpVerbosity::COMPACT;
          } else if (arg == "help -v") {
            verbosity = HelpVerbosity::VERBOSE;
          }

          print_parameter_help("x265", anicet::runner::x265::X265_PARAMETERS,
                              verbosity);
          exit(0);
        }

        // Accumulate parameter string for later parsing
        opt.x265_params.push_back(arg);
        break;
      }

      case 1001: {
        // Handle --webp option
        std::string arg(optarg);

        // Check for help requests
        if (arg == "help" || arg == "help -q" || arg == "help -v") {
          using namespace anicet::parameter;
          HelpVerbosity verbosity = HelpVerbosity::CONCISE;

          if (arg == "help -q") {
            verbosity = HelpVerbosity::COMPACT;
          } else if (arg == "help -v") {
            verbosity = HelpVerbosity::VERBOSE;
          }

          print_parameter_help("webp", anicet::runner::webp::WEBP_PARAMETERS,
                              verbosity);
          exit(0);
        }

        // Accumulate parameter string for later parsing
        opt.webp_params.push_back(arg);
        break;
      }

      case 1002: {
        // Handle --libjpeg-turbo option
        std::string arg(optarg);

        // Check for help requests
        if (arg == "help" || arg == "help -q" || arg == "help -v") {
          using namespace anicet::parameter;
          HelpVerbosity verbosity = HelpVerbosity::CONCISE;

          if (arg == "help -q") {
            verbosity = HelpVerbosity::COMPACT;
          } else if (arg == "help -v") {
            verbosity = HelpVerbosity::VERBOSE;
          }

          print_parameter_help("libjpeg-turbo", anicet::runner::libjpegturbo::LIBJPEGTURBO_PARAMETERS,
                              verbosity);
          exit(0);
        }

        // Accumulate parameter string for later parsing
        opt.libjpegturbo_params.push_back(arg);
        break;
      }

      case 1003: {
        // Handle --svt-av1 option
        std::string arg(optarg);

        // Check for help requests
        if (arg == "help" || arg == "help -q" || arg == "help -v") {
          using namespace anicet::parameter;
          HelpVerbosity verbosity = HelpVerbosity::CONCISE;

          if (arg == "help -q") {
            verbosity = HelpVerbosity::COMPACT;
          } else if (arg == "help -v") {
            verbosity = HelpVerbosity::VERBOSE;
          }

          print_parameter_help("svt-av1", anicet::runner::svtav1::SVTAV1_PARAMETERS,
                              verbosity);
          exit(0);
        }

        // Accumulate parameter string for later parsing
        opt.svtav1_params.push_back(arg);
        break;
      }

      case 1004: {
        // Handle --jpegli option
        std::string arg(optarg);

        // Check for help requests
        if (arg == "help" || arg == "help -q" || arg == "help -v") {
          using namespace anicet::parameter;
          HelpVerbosity verbosity = HelpVerbosity::CONCISE;

          if (arg == "help -q") {
            verbosity = HelpVerbosity::COMPACT;
          } else if (arg == "help -v") {
            verbosity = HelpVerbosity::VERBOSE;
          }

          print_parameter_help("jpegli", anicet::runner::jpegli::JPEGLI_PARAMETERS,
                              verbosity);
          exit(0);
        }

        // Accumulate parameter string for later parsing
        opt.jpegli_params.push_back(arg);
        break;
      }

      case 'N':
        opt.num_runs = atoi(optarg);
        if (opt.num_runs < 1) {
          fprintf(stderr, "--num-runs must be >= 1\n");
          return false;
        }
        break;

      case 'D':
        opt.dump_output = true;
        break;

      case 'O':
        opt.dump_output = false;
        break;

      case 'r':
        opt.dump_output_dir = optarg;
        break;

      case 'p':
        opt.dump_output_prefix = optarg;
        break;

      case 'o':
        opt.output_file = optarg;
        break;

      case 'd':
        opt.debug++;
        break;

      case 'q':
        opt.debug = 0;
        break;

      case '?':
        // getopt_long already printed an error message
        return false;

      default:
        return false;
    }
  }

  // Parse x265 parameters if provided (do this BEFORE command validation)
  if (!opt.x265_params.empty()) {
    // Initialize codec_setup with default values from descriptors
    opt.codec_setup.num_runs = opt.num_runs;  // Will be overridden by CLI if specified

    // Parse each parameter string (defaults will be applied only for explicitly set parameters)
    for (const auto& param_str : opt.x265_params) {
      if (!anicet::parameter::parse_parameter_string(
              "x265", param_str, anicet::runner::x265::X265_PARAMETERS,
              &opt.codec_setup)) {
        return false;
      }
    }

    // Validate parameter dependencies
    if (!anicet::parameter::validate_parameter_dependencies(
            "x265", anicet::runner::x265::X265_PARAMETERS, opt.codec_setup)) {
      return false;
    }
  }

  // Parse webp parameters if provided (do this BEFORE command validation)
  if (!opt.webp_params.empty()) {
    // Initialize codec_setup with default values from descriptors
    opt.codec_setup.num_runs = opt.num_runs;

    // Parse each parameter string
    for (const auto& param_str : opt.webp_params) {
      if (!anicet::parameter::parse_parameter_string(
              "webp", param_str, anicet::runner::webp::WEBP_PARAMETERS,
              &opt.codec_setup)) {
        return false;
      }
    }

    // Validate parameter dependencies
    if (!anicet::parameter::validate_parameter_dependencies(
            "webp", anicet::runner::webp::WEBP_PARAMETERS, opt.codec_setup)) {
      return false;
    }
  }

  // Parse libjpeg-turbo parameters if provided (do this BEFORE command validation)
  if (!opt.libjpegturbo_params.empty()) {
    // Initialize codec_setup with default values from descriptors
    opt.codec_setup.num_runs = opt.num_runs;

    // Parse each parameter string
    for (const auto& param_str : opt.libjpegturbo_params) {
      if (!anicet::parameter::parse_parameter_string(
              "libjpeg-turbo", param_str, anicet::runner::libjpegturbo::LIBJPEGTURBO_PARAMETERS,
              &opt.codec_setup)) {
        return false;
      }
    }

    // Validate parameter dependencies
    if (!anicet::parameter::validate_parameter_dependencies(
            "libjpeg-turbo", anicet::runner::libjpegturbo::LIBJPEGTURBO_PARAMETERS, opt.codec_setup)) {
      return false;
    }
  }

  // Parse svt-av1 parameters if provided (do this BEFORE command validation)
  if (!opt.svtav1_params.empty()) {
    // Initialize codec_setup with default values from descriptors
    opt.codec_setup.num_runs = opt.num_runs;

    // Parse each parameter string
    for (const auto& param_str : opt.svtav1_params) {
      if (!anicet::parameter::parse_parameter_string(
              "svt-av1", param_str, anicet::runner::svtav1::SVTAV1_PARAMETERS,
              &opt.codec_setup)) {
        return false;
      }
    }

    // Validate parameter dependencies
    if (!anicet::parameter::validate_parameter_dependencies(
            "svt-av1", anicet::runner::svtav1::SVTAV1_PARAMETERS, opt.codec_setup)) {
      return false;
    }
  }

  // Parse jpegli parameters if provided (do this BEFORE command validation)
  if (!opt.jpegli_params.empty()) {
    // Initialize codec_setup with default values from descriptors
    opt.codec_setup.num_runs = opt.num_runs;

    // Parse each parameter string
    for (const auto& param_str : opt.jpegli_params) {
      if (!anicet::parameter::parse_parameter_string(
              "jpegli", param_str, anicet::runner::jpegli::JPEGLI_PARAMETERS,
              &opt.codec_setup)) {
        return false;
      }
    }

    // Validate parameter dependencies
    if (!anicet::parameter::validate_parameter_dependencies(
            "jpegli", anicet::runner::jpegli::JPEGLI_PARAMETERS, opt.codec_setup)) {
      return false;
    }
  }

  // Command is optional if media parameters are provided
  bool has_media_params = !opt.image_file.empty() && opt.width > 0 &&
                          opt.height > 0 && !opt.color_format.empty();

  // Collect any remaining non-option arguments as command
  if (opt.debug >= 2) {
    fprintf(stderr, "DEBUG: optind=%d, new_argc=%d\n", optind, new_argc);
    for (int i = optind; i < new_argc; ++i) {
      fprintf(stderr, "DEBUG: new_argv[%d]='%s'\n", i, new_argv_ptr[i]);
    }
  }
  for (int i = optind; i < new_argc; ++i) {
    opt.cmd.emplace_back(new_argv_ptr[i]);
  }

  if (opt.cmd.empty()) {
    // No command provided - check if we have media parameters
    if (!has_media_params) {
      fprintf(stderr, "Missing -- and command, or --image/--width/--height/--color-format\n");
      return false;
    }
    return true;
  }

  // Can't have both command and media parameters
  if (has_media_params && !opt.cmd.empty()) {
    fprintf(stderr, "Cannot specify both command and media parameters\n");
    return false;
  }

  return true;
}


// global for signal forwarding
static pid_t g_child = -1;
static void relay_signal(int sig) {
  if (g_child > 0) {
    kill(g_child, sig);
  }
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

  // Get device serial number
  opt.serial_number = get_device_serial();

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
    if (!opt.cpus.empty()) {
      set_affinity_from_cpulist(opt.cpus);
    }
    if (opt.nice != 0) {
      set_nice(opt.nice);
    }

    // Set default dump_output_dir to executable directory if not specified
    if (opt.dump_output_dir.empty()) {
      opt.dump_output_dir = get_executable_dir();
    }

    // Set default dump_output_prefix if not specified
    if (opt.dump_output_prefix.empty()) {
      opt.dump_output_prefix = "anicet.output";
    }

    // Read image file
    std::string image_data;
    if (!read_file(opt.image_file, image_data)) {
      fprintf(stderr, "Failed to read image file: %s\n", opt.image_file.c_str());
      return 1;
    }

    // Create output structure to receive encoding results
    CodecOutput codec_output;

    // Call anicet_experiment()
    int result = anicet_experiment(
        reinterpret_cast<const uint8_t*>(image_data.data()),
        image_data.size(),
        opt.height,
        opt.width,
        opt.color_format.c_str(),
        opt.codec.c_str(),
        opt.num_runs,
        opt.dump_output,
        opt.dump_output_dir.c_str(),
        opt.dump_output_prefix.c_str(),
        opt.debug,
        &codec_output,
        (!opt.x265_params.empty() || !opt.webp_params.empty() || !opt.libjpegturbo_params.empty() || !opt.svtav1_params.empty() || !opt.jpegli_params.empty()) ? &opt.codec_setup : nullptr
    );

    // Print simple debug output to stdout if debug level >= 1
    if (opt.debug >= 1) {
      printf("input: %s\n", opt.image_file.c_str());
      printf("width: %d\n", opt.width);
      printf("height: %d\n", opt.height);
      printf("color_format: %s\n", opt.color_format.c_str());
      printf("size_bytes: %zu\n", image_data.size());
      printf("num_runs: %d\n", opt.num_runs);
      for (size_t i = 0; i < codec_output.num_frames(); i++) {
        printf("index: %zu\n", i);
        if (opt.dump_output && i < codec_output.output_files.size()) {
          printf("  file: %s\n", codec_output.output_files[i].c_str());
        }
        // Use codec name and parameters from CodecOutput
        printf("  codec: %s\n", codec_output.codec_name.c_str());
        // Print parameters in custom order for consistency
        auto sorted_params = get_sorted_params(codec_output.codec_params,
                                               codec_output.codec_name);
        for (const auto& [key, value] : sorted_params) {
          printf("  %s: %s\n", key.c_str(), value.c_str());
        }
        if (i < codec_output.frame_sizes.size()) {
          printf("  size_bytes: %zu\n", codec_output.frame_sizes[i]);
        }
        printf("  exit_code: %d\n", result);
      }
    }

    // Determine output file - default to stdout ("-")
    FILE* output_fp = stdout;
    bool close_output = false;

    if (opt.output_file != "-") {
      output_fp = fopen(opt.output_file.c_str(), "w");
      if (!output_fp) {
        fprintf(stderr, "Failed to open output file: %s\n", opt.output_file.c_str());
        output_fp = stdout;
      } else {
        close_output = true;
      }
    }

    // Build JSON output using nlohmann::json
    // Use ordered_json to preserve insertion order (input, setup, output, resources)
    using json = nlohmann::ordered_json;
    json output_json;

    // Input section
    output_json["input"] = {
      {"file", opt.image_file},
      {"width", opt.width},
      {"height", opt.height},
      {"color_format", opt.color_format},
      {"size_bytes", image_data.size()}
    };

    // Setup section
    output_json["setup"]["serial_number"] = opt.serial_number;
    output_json["setup"]["num_runs"] = opt.num_runs;
    // Add device and other tags to setup section if present
    for (const auto& kv : opt.tags) {
      output_json["setup"][kv.first] = kv.second;
    }

    // Output section - frames array with codec, params, exit_code and size_bytes per frame
    output_json["output"]["frames"] = json::array();
    for (size_t i = 0; i < codec_output.num_frames(); i++) {
      json output_frame;
      if (opt.dump_output && i < codec_output.output_files.size()) {
        output_frame["file"] = codec_output.output_files[i];
      }

      // Use codec name and parameters from CodecOutput
      output_frame["codec"] = codec_output.codec_name;
      // Add parameters to frame in custom order
      auto sorted_params = get_sorted_params(codec_output.codec_params,
                                             codec_output.codec_name);
      for (const auto& [key, value] : sorted_params) {
        output_frame[key] = value;
      }

      output_frame["exit_code"] = result;
      if (i < codec_output.frame_sizes.size()) {
        output_frame["size_bytes"] = codec_output.frame_sizes[i];
      }
      output_json["output"]["frames"].push_back(output_frame);
    }

    // Resources section - global
    // Use resource_delta for detailed metrics
    const ResourceDelta& delta = codec_output.resource_delta;
    output_json["resources"]["global"]["wall_time_ms"] = delta.wall_time_ms;

    // CPU time breakdown
    output_json["resources"]["global"]["cpu_time"]["total_ms"] = delta.cpu_time_ms;
    output_json["resources"]["global"]["cpu_time"]["user_time_ms"] = delta.user_time_ms;
    output_json["resources"]["global"]["cpu_time"]["system_time_ms"] = delta.system_time_ms;

    // CPU utilization percentage
    if (delta.wall_time_ms > 0) {
      output_json["resources"]["global"]["cpu_time"]["utilization_percent"] =
        (delta.cpu_time_ms / delta.wall_time_ms * 100.0);
    }

    // Memory statistics
    output_json["resources"]["global"]["memory_rss_kb"] = delta.vm_rss_delta_kb;
    output_json["resources"]["global"]["memory_vss_kb"] = delta.vm_size_delta_kb;

    // Page faults
    output_json["resources"]["global"]["page_faults"]["minor"] = delta.minor_faults;
    output_json["resources"]["global"]["page_faults"]["major"] = delta.major_faults;

    // Context switches
    output_json["resources"]["global"]["context_switches"]["voluntary"] = delta.vol_ctx_switches;
    output_json["resources"]["global"]["context_switches"]["involuntary"] = delta.invol_ctx_switches;

    // Frames array
    output_json["resources"]["frames"] = json::array();
    for (size_t i = 0; i < codec_output.num_frames(); i++) {
      json frame;
      frame["frame_index"] = i;

      // Frame size
      if (i < codec_output.frame_sizes.size()) {
        frame["size_bytes"] = codec_output.frame_sizes[i];
      }

      // Frame timing
      if (i < codec_output.timings.size()) {
        frame["input_timestamp_us"] = codec_output.timings[i].input_timestamp_us;
        frame["output_timestamp_us"] = codec_output.timings[i].output_timestamp_us;
        int64_t encode_time_us = codec_output.timings[i].output_timestamp_us -
                                 codec_output.timings[i].input_timestamp_us;
        frame["encode_time_us"] = encode_time_us;
      }

      // CPU time for this frame
      if (i < codec_output.profile_encode_cpu_ms.size()) {
        frame["cpu_time_ms"] = codec_output.profile_encode_cpu_ms[i];
      }

      output_json["resources"]["frames"].push_back(frame);
    }

    // Output JSON to file with pretty printing (2-space indent)
    fprintf(output_fp, "%s\n", output_json.dump(2).c_str());

    if (close_output) {
      fclose(output_fp);
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
    if (!opt.cpus.empty()) {
      set_affinity_from_cpulist(opt.cpus);
    }
    if (opt.nice != 0) {
      set_nice(opt.nice);
    }

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
    if (read_file(path, status)) {
      vmhwm_kb = parse_vmhwm_kb_from_status(status);
    }
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
  if (WIFEXITED(status_code)) {
    exit_code = WEXITSTATUS(status_code);
  } else if (WIFSIGNALED(status_code)) {
    exit_code = 128 + WTERMSIG(status_code);
  }

  // Killed
  if (timed_out && exit_code == -1) {
    exit_code = 137;
  }

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
        if (c == '"' || c == '\\') {
          putchar('\\');
        }
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
    // no tags
    if (first) {
      printf("run=na");
    }
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
