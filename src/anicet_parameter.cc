// anicet_parameter.cc
// Parameter descriptor system implementation

#include "anicet_parameter.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace anicet {
namespace parameter {

// Helper: Split string by delimiter
static std::vector<std::string> split(const std::string& s, char delimiter) {
  std::vector<std::string> tokens;
  std::string token;
  std::istringstream tokenStream(s);
  while (std::getline(tokenStream, token, delimiter)) {
    tokens.push_back(token);
  }
  return tokens;
}

// Helper: Trim whitespace from string
static std::string trim(const std::string& s) {
  size_t start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  size_t end = s.find_last_not_of(" \t\n\r");
  return s.substr(start, end - start + 1);
}

// Validate and set a single parameter
bool validate_and_set_parameter(const std::string& codec_name,
                                const std::string& param_name,
                                const std::string& param_value,
                                const ParameterDescriptor& descriptor,
                                CodecSetup* setup) {
  switch (descriptor.type) {
    case ParameterType::STRING_LIST: {
      // Use existing validate_parameter_list function
      if (!validate_parameter_list(codec_name, param_name, param_value,
                                   descriptor.valid_values)) {
        return false;
      }
      setup->parameter_map[param_name] = param_value;
      break;
    }

    case ParameterType::INTEGER_RANGE: {
      try {
        int val = std::stoi(param_value);
        int min = std::get<int>(descriptor.min_value);
        int max = std::get<int>(descriptor.max_value);
        if (val < min || val > max) {
          fprintf(stderr, "%s: Invalid value '%d' for parameter '%s'\n",
                  codec_name.c_str(), val, param_name.c_str());
          fprintf(stderr, "Valid range: %d-%d\n", min, max);
          return false;
        }
        setup->parameter_map[param_name] = val;
      } catch (const std::exception&) {
        fprintf(stderr, "%s: Invalid integer value for parameter '%s': '%s'\n",
                codec_name.c_str(), param_name.c_str(), param_value.c_str());
        return false;
      }
      break;
    }

    case ParameterType::DOUBLE_RANGE: {
      try {
        double val = std::stod(param_value);
        double min = std::get<double>(descriptor.min_value);
        double max = std::get<double>(descriptor.max_value);
        if (val < min || val > max) {
          fprintf(stderr, "%s: Invalid value '%.2f' for parameter '%s'\n",
                  codec_name.c_str(), val, param_name.c_str());
          fprintf(stderr, "Valid range: %.2f-%.2f\n", min, max);
          return false;
        }
        setup->parameter_map[param_name] = val;
      } catch (const std::exception&) {
        fprintf(stderr, "%s: Invalid numeric value for parameter '%s': '%s'\n",
                codec_name.c_str(), param_name.c_str(), param_value.c_str());
        return false;
      }
      break;
    }
  }

  return true;
}

// Parse parameter string
bool parse_parameter_string(
    const std::string& codec_name, const std::string& param_string,
    const std::map<std::string, ParameterDescriptor>& descriptors,
    CodecSetup* setup) {
  // Split by both colon and comma for compatibility
  // Try colon first, fall back to comma
  std::vector<std::string> pairs;
  if (param_string.find(':') != std::string::npos) {
    pairs = split(param_string, ':');
  } else {
    pairs = split(param_string, ',');
  }

  for (const auto& pair : pairs) {
    std::string trimmed_pair = trim(pair);
    if (trimmed_pair.empty()) continue;

    // Split by '='
    size_t eq_pos = trimmed_pair.find('=');
    if (eq_pos == std::string::npos) {
      fprintf(stderr, "%s: Invalid parameter format '%s'\n", codec_name.c_str(),
              trimmed_pair.c_str());
      fprintf(stderr, "Expected format: key=value\n");
      return false;
    }

    std::string key = trim(trimmed_pair.substr(0, eq_pos));
    std::string value = trim(trimmed_pair.substr(eq_pos + 1));

    // Check if parameter exists
    auto it = descriptors.find(key);
    if (it == descriptors.end()) {
      fprintf(stderr, "%s: Unknown parameter '%s'\n", codec_name.c_str(),
              key.c_str());
      fprintf(stderr, "Run '--%s help' for available parameters\n",
              codec_name.c_str());
      return false;
    }

    // Validate and set
    if (!validate_and_set_parameter(codec_name, key, value, it->second,
                                    setup)) {
      return false;
    }
  }

  return true;
}

// Validate parameter dependencies
bool validate_parameter_dependencies(
    const std::string& codec_name,
    const std::map<std::string, ParameterDescriptor>& descriptors,
    const CodecSetup& setup) {
  // Check each set parameter has its dependencies satisfied
  for (const auto& [param_name, param_value] : setup.parameter_map) {
    auto desc_it = descriptors.find(param_name);
    if (desc_it == descriptors.end())
      continue;  // Skip unknown (shouldn't happen)

    const auto& descriptor = desc_it->second;

    // Check if this parameter has dependency requirements
    if (descriptor.requires_param.has_value()) {
      const std::string& req_param = descriptor.requires_param.value();
      const std::string& req_value = descriptor.requires_value.value();

      // Check if required parameter is set with required value
      auto req_it = setup.parameter_map.find(req_param);
      if (req_it == setup.parameter_map.end()) {
        fprintf(stderr, "%s: Parameter '%s' requires '%s' to be set\n",
                codec_name.c_str(), param_name.c_str(), req_param.c_str());
        return false;
      }

      // Check if the value matches
      try {
        const std::string& actual_value = std::get<std::string>(req_it->second);
        if (actual_value != req_value) {
          fprintf(stderr, "%s: Cannot use '%s' when %s=%s\n",
                  codec_name.c_str(), param_name.c_str(), req_param.c_str(),
                  actual_value.c_str());
          fprintf(stderr, "Parameter '%s' requires: %s=%s\n",
                  param_name.c_str(), req_param.c_str(), req_value.c_str());
          return false;
        }
      } catch (const std::bad_variant_access&) {
        // Required param is not a string, skip check
        continue;
      }
    }
  }

  return true;
}

// Helper: Format value for display
static std::string format_value(const CodecSetupValue& value) {
  if (std::holds_alternative<int>(value)) {
    return std::to_string(std::get<int>(value));
  } else if (std::holds_alternative<double>(value)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", std::get<double>(value));
    return std::string(buf);
  } else if (std::holds_alternative<std::string>(value)) {
    return std::get<std::string>(value);
  }
  return "unknown";
}

// Helper: Sort descriptors by order field, then alphabetically
static std::vector<std::pair<std::string, ParameterDescriptor>>
sort_descriptors_by_order(
    const std::map<std::string, ParameterDescriptor>& descriptors) {
  std::vector<std::pair<std::string, ParameterDescriptor>> sorted(
      descriptors.begin(), descriptors.end());
  std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
    if (a.second.order != b.second.order) {
      return a.second.order < b.second.order;
    }
    return a.first < b.first;  // Alphabetical for same order
  });
  return sorted;
}

// Print compact help
static void print_help_compact(
    const std::string& codec_name,
    const std::map<std::string, ParameterDescriptor>& descriptors) {
  printf("%s parameters: ", codec_name.c_str());

  auto sorted_descriptors = sort_descriptors_by_order(descriptors);

  bool first = true;
  for (const auto& [name, desc] : sorted_descriptors) {
    if (!first) printf(", ");
    first = false;

    printf("%s=", name.c_str());

    switch (desc.type) {
      case ParameterType::STRING_LIST: {
        printf("{");
        bool first_val = true;
        for (const auto& val : desc.valid_values) {
          if (!first_val) printf("|");
          first_val = false;
          printf("%s", val.c_str());
        }
        printf("}");
        break;
      }
      case ParameterType::INTEGER_RANGE:
        printf("%d-%d", std::get<int>(desc.min_value),
               std::get<int>(desc.max_value));
        break;
      case ParameterType::DOUBLE_RANGE:
        printf("%.2f-%.2f", std::get<double>(desc.min_value),
               std::get<double>(desc.max_value));
        break;
    }
  }
  printf("\n");
}

// Print concise help
static void print_help_concise(
    const std::string& codec_name,
    const std::map<std::string, ParameterDescriptor>& descriptors) {
  printf("Available parameters for %s:\n\n", codec_name.c_str());

  auto sorted_descriptors = sort_descriptors_by_order(descriptors);

  for (const auto& [name, desc] : sorted_descriptors) {
    printf("  %-15s %s\n", name.c_str(), desc.description.c_str());

    switch (desc.type) {
      case ParameterType::STRING_LIST: {
        printf("                  Values: ");
        bool first = true;
        for (const auto& val : desc.valid_values) {
          if (!first) printf(", ");
          first = false;
          printf("%s", val.c_str());
        }
        printf(" (default: %s)\n", format_value(desc.default_value).c_str());
        break;
      }
      case ParameterType::INTEGER_RANGE:
        printf("                  Range: %d-%d (default: %s)\n",
               std::get<int>(desc.min_value), std::get<int>(desc.max_value),
               format_value(desc.default_value).c_str());
        break;
      case ParameterType::DOUBLE_RANGE:
        printf("                  Range: %.2f-%.2f (default: %s)\n",
               std::get<double>(desc.min_value),
               std::get<double>(desc.max_value),
               format_value(desc.default_value).c_str());
        break;
    }

    // Print dependency note if exists
    if (desc.requires_param.has_value()) {
      printf("                  Note: Requires %s=%s\n",
             desc.requires_param.value().c_str(),
             desc.requires_value.value().c_str());
    }

    printf("\n");
  }

  printf("Usage: --%s param=value:param=value:...\n", codec_name.c_str());
  printf("   or: --%s param=value --%s param=value ...\n", codec_name.c_str(),
         codec_name.c_str());

  // Codec-specific examples
  if (codec_name == "x265") {
    printf("Example: --%s optimization=opt:preset=ultrafast:qp=30\n",
           codec_name.c_str());
  } else if (codec_name == "webp") {
    printf("Example: --%s optimization=opt:quality=90:method=6\n",
           codec_name.c_str());
  } else if (codec_name == "libjpeg-turbo") {
    printf("Example: --%s optimization=opt:quality=90\n", codec_name.c_str());
  } else if (codec_name == "svt-av1") {
    printf("Example: --%s preset=8:qp=35\n", codec_name.c_str());
  } else if (codec_name == "jpegli") {
    printf("Example: --%s quality=75\n", codec_name.c_str());
  } else {
    printf("Example: --%s param1=value1:param2=value2\n", codec_name.c_str());
  }
}

// Print verbose help
static void print_help_verbose(
    const std::string& codec_name,
    const std::map<std::string, ParameterDescriptor>& descriptors) {
  printf("%s Encoder Parameters\n", codec_name.c_str());
  printf("=======================\n\n");

  auto sorted_descriptors = sort_descriptors_by_order(descriptors);

  for (const auto& [name, desc] : sorted_descriptors) {
    // Convert to uppercase for header
    std::string upper_name = desc.description;
    for (char& c : upper_name) c = toupper(c);

    printf("%s (%s)\n", upper_name.c_str(), name.c_str());
    printf("  Description: %s\n", desc.description.c_str());

    switch (desc.type) {
      case ParameterType::STRING_LIST: {
        printf("  Type: String (choice)\n");
        printf("  Valid values:\n");
        for (const auto& val : desc.valid_values) {
          printf("    - %s\n", val.c_str());
        }
        break;
      }
      case ParameterType::INTEGER_RANGE:
        printf("  Type: Integer\n");
        printf("  Range: %d to %d\n", std::get<int>(desc.min_value),
               std::get<int>(desc.max_value));
        break;
      case ParameterType::DOUBLE_RANGE:
        printf("  Type: Numeric\n");
        printf("  Range: %.2f to %.2f\n", std::get<double>(desc.min_value),
               std::get<double>(desc.max_value));
        break;
    }

    printf("  Default: %s\n", format_value(desc.default_value).c_str());

    if (desc.requires_param.has_value()) {
      printf("  Requires: %s=%s\n", desc.requires_param.value().c_str(),
             desc.requires_value.value().c_str());
    }

    printf("\n");
  }

  printf("USAGE\n");
  printf("-----\n");
  printf("--%s param=value:param=value:...\n", codec_name.c_str());
  printf("--%s param=value --%s param=value ...\n\n", codec_name.c_str(),
         codec_name.c_str());

  printf("EXAMPLE\n");
  printf("-------\n");
  if (codec_name == "x265") {
    printf("--%s optimization=opt:preset=ultrafast:qp=30\n\n",
           codec_name.c_str());
  } else if (codec_name == "webp") {
    printf("--%s optimization=opt:quality=90:method=6\n\n", codec_name.c_str());
  } else if (codec_name == "libjpeg-turbo") {
    printf("--%s optimization=opt:quality=90\n\n", codec_name.c_str());
  } else if (codec_name == "svt-av1") {
    printf("--%s preset=8:qp=35\n\n", codec_name.c_str());
  } else if (codec_name == "jpegli") {
    printf("--%s quality=75\n\n", codec_name.c_str());
  } else {
    printf("--%s param1=value1:param2=value2\n\n", codec_name.c_str());
  }
}

// Print parameter help
void print_parameter_help(
    const std::string& codec_name,
    const std::map<std::string, ParameterDescriptor>& descriptors,
    HelpVerbosity verbosity) {
  switch (verbosity) {
    case HelpVerbosity::COMPACT:
      print_help_compact(codec_name, descriptors);
      break;
    case HelpVerbosity::CONCISE:
      print_help_concise(codec_name, descriptors);
      break;
    case HelpVerbosity::VERBOSE:
      print_help_verbose(codec_name, descriptors);
      break;
  }
}

}  // namespace parameter
}  // namespace anicet
