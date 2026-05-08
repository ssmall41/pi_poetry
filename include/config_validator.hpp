#pragma once
#include <toml++/toml.hpp>
#include <string>
#include <vector>

std::vector<std::string> validate_config(const toml::table& config);
