#ifndef FOREVERTAS_SEARCHES_OPTION_CONFIGURATION_H
#define FOREVERTAS_SEARCHES_OPTION_CONFIGURATION_H

#include <map>
#include <string>

namespace forevertas {

using OptionSettings = std::map<std::string, std::string>;

struct OptionConfiguration {
    std::string id;
    OptionSettings settings;
};

}  // namespace forevertas

#endif
