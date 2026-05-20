#pragma once

#include <string>

std::string format_date_range(const std::string &birth_date, const std::string &death_date);
std::string format_person_name_parts(const std::string &first_name,
                                     const std::string &middle_name,
                                     const std::string &last_name);

// Validates and normalizes date to ISO 8601 format (YYYY-MM-DD)
// Returns the normalized date if valid, or empty string if invalid/empty
std::string validate_and_normalize_date(const std::string &date_input);
