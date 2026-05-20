#include "formatters.h"

#include <regex>

std::string format_date_range(const std::string &birth_date, const std::string &death_date)
{
    if (birth_date.empty() && death_date.empty()) {
        return "";
    }

    if (birth_date.empty()) {
        return death_date;
    }

    if (!death_date.empty()) {
        return birth_date + " - " + death_date;
    }

    return birth_date;
}

std::string format_person_name_parts(const std::string &first_name,
                                     const std::string &middle_name,
                                     const std::string &last_name)
{
    std::string result;

    auto append_part = [&result](const std::string &part) {
        if (part.empty()) {
            return;
        }

        if (!result.empty()) {
            result += ' ';
        }

        result += part;
    };

    append_part(first_name);
    append_part(middle_name);
    append_part(last_name);
    return result;
}

std::string validate_and_normalize_date(const std::string &date_input)
{
    if (date_input.empty()) {
        return "";
    }

    // ISO 8601 format (YYYY-MM-DD) - already correct
    if (std::regex_match(date_input, std::regex(R"(\d{4}-\d{2}-\d{2})"))) {
        return date_input;
    }

    // Try to parse DD.MM.YYYY format
    std::smatch match;
    if (std::regex_match(date_input, match, std::regex(R"((\d{1,2})\.(\d{1,2})\.(\d{4}))"))) {
        std::string day = match[1].str();
        std::string month = match[2].str();
        std::string year = match[3].str();
        
        // Pad day and month with leading zero if needed
        if (day.length() == 1) day = "0" + day;
        if (month.length() == 1) month = "0" + month;
        
        return year + "-" + month + "-" + day;
    }

    // Try to parse X/X/YYYY format (could be DD/MM/YYYY or MM/DD/YYYY)
    if (std::regex_match(date_input, match, std::regex(R"((\d{1,2})/(\d{1,2})/(\d{4}))"))) {
        int first = std::stoi(match[1].str());
        int second = std::stoi(match[2].str());
        
        std::string month, day, year = match[3].str();
        
        if (first <= 12 && second > 12) {
            // Clearly MM/DD/YYYY (month is ≤12, day is >12)
            month = match[1].str();
            day = match[2].str();
        } else if (first > 12 && second <= 12) {
            // Clearly DD/MM/YYYY (day is >12, month is ≤12)
            day = match[1].str();
            month = match[2].str();
        } else {
            // Ambiguous - assume DD/MM/YYYY (European default)
            day = match[1].str();
            month = match[2].str();
        }
        
        if (month.length() == 1) month = "0" + month;
        if (day.length() == 1) day = "0" + day;
        
        return year + "-" + month + "-" + day;
    }

    // Invalid date format - return empty string
    return "";
}
