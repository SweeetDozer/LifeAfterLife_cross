#include "formatters.h"

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
