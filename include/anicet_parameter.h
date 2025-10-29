// anicet_parameter.h
// Parameter descriptor system for codec configuration

#ifndef ANICET_PARAMETER_H
#define ANICET_PARAMETER_H

#ifdef __cplusplus

#include <list>
#include <map>
#include <optional>
#include <string>
#include <variant>

#include "anicet_runner.h"

namespace anicet {
namespace parameter {

// Parameter type enumeration
enum class ParameterType {
  STRING_LIST,    // String with predefined valid values
  INTEGER_RANGE,  // Integer with min/max range
  DOUBLE_RANGE,   // Double with min/max range
};

// Parameter descriptor
struct ParameterDescriptor {
  std::string name;
  ParameterType type;
  std::string description;

  // For STRING_LIST type
  std::list<std::string> valid_values;

  // For INTEGER_RANGE / DOUBLE_RANGE types
  std::variant<int, double> min_value;
  std::variant<int, double> max_value;

  // Default value
  CodecSetupValue default_value;

  // Optional parameter dependencies
  std::optional<std::string> requires_param;  // Parameter name required
  std::optional<std::string> requires_value;  // Required value for that param

  // Display order (lower values appear first, 100 is default for unspecified)
  int order = 100;
};

// Parse comma or colon-separated parameter string "key=value:key=value"
// Returns true on success, false on error (with error message printed)
bool parse_parameter_string(
    const std::string& codec_name, const std::string& param_string,
    const std::map<std::string, ParameterDescriptor>& descriptors,
    CodecSetup* setup);

// Validate and set a single parameter value
// Returns true on success, false on error (with error message printed)
bool validate_and_set_parameter(const std::string& codec_name,
                                const std::string& param_name,
                                const std::string& param_value,
                                const ParameterDescriptor& descriptor,
                                CodecSetup* setup);

// Validate parameter dependencies after all parameters are set
// Returns true if all dependencies satisfied, false with error message
bool validate_parameter_dependencies(
    const std::string& codec_name,
    const std::map<std::string, ParameterDescriptor>& descriptors,
    const CodecSetup& setup);

// Print parameter help with configurable verbosity
enum class HelpVerbosity {
  COMPACT,  // One-liner
  CONCISE,  // Default, clean multi-line
  VERBOSE   // Detailed descriptions
};

void print_parameter_help(
    const std::string& codec_name,
    const std::map<std::string, ParameterDescriptor>& descriptors,
    HelpVerbosity verbosity = HelpVerbosity::CONCISE);

}  // namespace parameter
}  // namespace anicet

#endif  // __cplusplus

#endif  // ANICET_PARAMETER_H
