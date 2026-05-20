#include "persistence.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>
#include <variant>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace storage {

namespace {

constexpr int kPersistenceVersion = 1;

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue {
    using Variant = std::variant<std::nullptr_t, bool, double, std::string, JsonObject>;
    Variant value;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {}

    std::optional<JsonValue> parse() {
        skip_whitespace();
        auto value = parse_value();
        if (!value) {
            return std::nullopt;
        }
        skip_whitespace();
        if (position_ != source_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<JsonValue> parse_value() {
        skip_whitespace();
        if (position_ >= source_.size()) {
            return std::nullopt;
        }

        const char current = source_[position_];
        if (current == '{') {
            return parse_object();
        }
        if (current == '"') {
            auto parsed_string = parse_string();
            if (!parsed_string) {
                return std::nullopt;
            }
            return JsonValue { *parsed_string };
        }
        if (current == 't') {
            return parse_literal("true", JsonValue { true });
        }
        if (current == 'f') {
            return parse_literal("false", JsonValue { false });
        }
        if (current == 'n') {
            return parse_literal("null", JsonValue { nullptr });
        }
        if (current == '-' || (current >= '0' && current <= '9')) {
            auto parsed_number = parse_number();
            if (!parsed_number) {
                return std::nullopt;
            }
            return JsonValue { *parsed_number };
        }

        return std::nullopt;
    }

    std::optional<JsonValue> parse_object() {
        if (!consume('{')) {
            return std::nullopt;
        }

        JsonObject object;
        skip_whitespace();
        if (consume('}')) {
            return JsonValue { object };
        }

        while (position_ < source_.size()) {
            skip_whitespace();
            auto key = parse_string();
            if (!key) {
                return std::nullopt;
            }

            skip_whitespace();
            if (!consume(':')) {
                return std::nullopt;
            }

            skip_whitespace();
            auto value = parse_value();
            if (!value) {
                return std::nullopt;
            }

            object.emplace(*key, *value);
            skip_whitespace();
            if (consume('}')) {
                return JsonValue { object };
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    std::optional<std::string> parse_string() {
        if (!consume('"')) {
            return std::nullopt;
        }

        std::string result;
        while (position_ < source_.size()) {
            const char current = source_[position_++];
            if (current == '"') {
                return result;
            }
            if (current == '\\') {
                if (position_ >= source_.size()) {
                    return std::nullopt;
                }
                const char escaped = source_[position_++];
                switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: return std::nullopt;
                }
                continue;
            }

            result.push_back(current);
        }

        return std::nullopt;
    }

    std::optional<double> parse_number() {
        const std::size_t start = position_;
        if (source_[position_] == '-') {
            ++position_;
        }

        if (position_ >= source_.size()) {
            return std::nullopt;
        }

        if (source_[position_] == '0') {
            ++position_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                return std::nullopt;
            }
            while (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            if (position_ >= source_.size() || !std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                return std::nullopt;
            }
            while (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            ++position_;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= source_.size() || !std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                return std::nullopt;
            }
            while (position_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        double parsed_value = 0.0;
        const std::string_view token = source_.substr(start, position_ - start);
        const auto [ptr, error] = std::from_chars(token.data(), token.data() + token.size(), parsed_value);
        if (error == std::errc() && ptr == token.data() + token.size()) {
            return parsed_value;
        }

        std::string token_string(token);
        char* end = nullptr;
        parsed_value = std::strtod(token_string.c_str(), &end);
        if (end != token_string.c_str() + token_string.size()) {
            return std::nullopt;
        }
        return parsed_value;
    }

    std::optional<JsonValue> parse_literal(std::string_view literal, JsonValue result) {
        if (source_.substr(position_, literal.size()) != literal) {
            return std::nullopt;
        }
        position_ += literal.size();
        return result;
    }

    void skip_whitespace() {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < source_.size() && source_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    std::string_view source_;
    std::size_t position_ = 0;
};

const JsonObject* as_object(const JsonValue* value) {
    return value && std::holds_alternative<JsonObject>(value->value)
        ? &std::get<JsonObject>(value->value)
        : nullptr;
}

const JsonValue* find_member(const JsonObject* object, const std::string& key) {
    if (!object) {
        return nullptr;
    }
    const auto iterator = object->find(key);
    return iterator != object->end() ? &iterator->second : nullptr;
}

std::optional<std::string> get_string(const JsonObject* object, const std::string& key) {
    const JsonValue* value = find_member(object, key);
    if (value && std::holds_alternative<std::string>(value->value)) {
        return std::get<std::string>(value->value);
    }
    return std::nullopt;
}

std::optional<double> get_number(const JsonObject* object, const std::string& key) {
    const JsonValue* value = find_member(object, key);
    if (value && std::holds_alternative<double>(value->value)) {
        return std::get<double>(value->value);
    }
    return std::nullopt;
}

std::optional<int> get_int(const JsonObject* object, const std::string& key) {
    auto number = get_number(object, key);
    if (!number) {
        return std::nullopt;
    }
    const double rounded = std::round(*number);
    if (std::fabs(*number - rounded) > 0.000001) {
        return std::nullopt;
    }
    return static_cast<int>(rounded);
}

bool has_supported_version(const JsonObject& object, const std::filesystem::path& file_path)
{
    const auto version = get_int(&object, "version");
    if (!version.has_value()) {
        std::cerr << "Persistence warning: missing version field in '" << file_path.string()
                  << "'. Falling back to defaults.\n";
        return false;
    }

    if (*version != kPersistenceVersion) {
        std::cerr << "Persistence warning: unsupported persistence version " << *version
                  << " in '" << file_path.string() << "'. Expected " << kPersistenceVersion
                  << ". Falling back to defaults.\n";
        return false;
    }

    return true;
}

std::string escape_json_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

std::string indent(int level) {
    return std::string(static_cast<std::size_t>(level) * 2U, ' ');
}

std::string format_number(double value) {
    std::ostringstream stream;
    stream << std::setprecision(15) << value;
    std::string formatted = stream.str();
    if (formatted.find('.') != std::string::npos) {
        while (!formatted.empty() && formatted.back() == '0') {
            formatted.pop_back();
        }
        if (!formatted.empty() && formatted.back() == '.') {
            formatted.push_back('0');
        }
    }
    return formatted;
}

void append_json(std::string& output, const JsonValue& value, int level) {
    if (std::holds_alternative<std::nullptr_t>(value.value)) {
        output += "null";
        return;
    }
    if (std::holds_alternative<bool>(value.value)) {
        output += std::get<bool>(value.value) ? "true" : "false";
        return;
    }
    if (std::holds_alternative<double>(value.value)) {
        output += format_number(std::get<double>(value.value));
        return;
    }
    if (std::holds_alternative<std::string>(value.value)) {
        output += '"';
        output += escape_json_string(std::get<std::string>(value.value));
        output += '"';
        return;
    }

    const JsonObject& object = std::get<JsonObject>(value.value);
    output += "{\n";
    bool first = true;
    for (const auto& [key, object_value] : object) {
        if (!first) {
            output += ",\n";
        }
        first = false;
        output += indent(level + 1);
        output += '"';
        output += escape_json_string(key);
        output += "\": ";
        append_json(output, object_value, level + 1);
    }
    output += '\n';
    output += indent(level);
    output += '}';
}

std::string serialize_json(const JsonObject& object) {
    std::string output;
    append_json(output, JsonValue { object }, 0);
    output += '\n';
    return output;
}

bool replace_file_atomically(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (MoveFileExW(source.wstring().c_str(), destination.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return true;
    }
    std::cerr << "Persistence warning: failed to replace file '" << destination.string()
              << "' with temp file '" << source.string() << "'.\n";
    return false;
#else
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (!error) {
        return true;
    }
    std::cerr << "Persistence warning: failed to replace file '" << destination.string()
              << "' with temp file '" << source.string() << "': " << error.message() << "\n";
    return false;
#endif
}

std::optional<JsonObject> parse_json_object(std::string_view json_text, const std::filesystem::path& file_path) {
    JsonParser parser(json_text);
    auto parsed = parser.parse();
    const JsonObject* object = as_object(parsed ? &*parsed : nullptr);
    if (!object) {
        std::cerr << "Persistence warning: invalid JSON in '" << file_path.string() << "'. Falling back to defaults.\n";
        return std::nullopt;
    }
    return *object;
}

SessionData default_session() {
    return {};
}

UiStateData default_ui_state() {
    return {};
}

TreeLayoutData default_tree_layout() {
    return {};
}

} // namespace

std::filesystem::path find_existing_app_data_directory(std::filesystem::path path)
{
    while (!path.empty()) {
        const auto candidate = path / "app_data";
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        const auto parent = path.parent_path();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return {};
}

std::filesystem::path default_app_data_directory() {
    const auto current_app_data = std::filesystem::current_path() / "app_data";
    if (std::filesystem::exists(current_app_data)) {
        return current_app_data;
    }

    std::filesystem::path executable_dir;
#ifdef _WIN32
    wchar_t module_path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        executable_dir = std::filesystem::path(module_path).parent_path();
    }
#endif

    if (!executable_dir.empty()) {
        const auto found = find_existing_app_data_directory(executable_dir);
        if (!found.empty()) {
            return found;
        }
    }

    return current_app_data;
}

std::filesystem::path session_file_path(const std::filesystem::path& app_data_directory) {
    return app_data_directory / "session.json";
}

std::filesystem::path ui_state_file_path(const std::filesystem::path& app_data_directory) {
    return app_data_directory / "ui_state.json";
}

std::filesystem::path tree_layouts_directory(const std::filesystem::path& app_data_directory) {
    return app_data_directory / "tree_layouts";
}

std::filesystem::path tree_layout_file_path(const std::filesystem::path& app_data_directory, int tree_id) {
    return tree_layouts_directory(app_data_directory) / ("tree_" + std::to_string(tree_id) + ".json");
}

bool ensure_app_data_directories(const std::filesystem::path& app_data_directory) {
    std::error_code error;
    std::filesystem::create_directories(app_data_directory, error);
    if (error) {
        std::cerr << "Persistence warning: failed to create app data directory '"
                  << app_data_directory.string() << "': " << error.message() << "\n";
        return false;
    }

    std::filesystem::create_directories(tree_layouts_directory(app_data_directory), error);
    if (error) {
        std::cerr << "Persistence warning: failed to create tree layouts directory '"
                  << tree_layouts_directory(app_data_directory).string() << "': " << error.message() << "\n";
        return false;
    }

    return true;
}

std::optional<std::string> load_json_text(const std::filesystem::path& file_path) {
    std::ifstream input(file_path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        std::cerr << "Persistence warning: failed to read '" << file_path.string() << "'.\n";
        return std::nullopt;
    }

    return buffer.str();
}

bool save_json_text_atomic(const std::filesystem::path& file_path, std::string_view json_text) {
    std::error_code error;
    std::filesystem::create_directories(file_path.parent_path(), error);
    if (error) {
        std::cerr << "Persistence warning: failed to create parent directory for '"
                  << file_path.string() << "': " << error.message() << "\n";
        return false;
    }

    const std::filesystem::path temp_path = file_path.string() + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            std::cerr << "Persistence warning: failed to open temp file '" << temp_path.string() << "' for writing.\n";
            return false;
        }

        output.write(json_text.data(), static_cast<std::streamsize>(json_text.size()));
        output.flush();
        if (!output.good()) {
            std::cerr << "Persistence warning: failed to write temp file '" << temp_path.string() << "'.\n";
            output.close();
            std::filesystem::remove(temp_path, error);
            return false;
        }
    }

    if (!replace_file_atomically(temp_path, file_path)) {
        std::filesystem::remove(temp_path, error);
        return false;
    }

    return true;
}

SessionData load_session(const std::filesystem::path& app_data_directory) {
    ensure_app_data_directories(app_data_directory);
    auto json_text = load_json_text(session_file_path(app_data_directory));
    if (!json_text) {
        return default_session();
    }

    auto root = parse_json_object(*json_text, session_file_path(app_data_directory));
    if (!root) {
        return default_session();
    }
    if (!has_supported_version(*root, session_file_path(app_data_directory))) {
        return default_session();
    }

    SessionData session;
    session.access_token = get_string(&*root, "access_token").value_or("");
    session.refresh_token = get_string(&*root, "refresh_token").value_or("");
    session.email = get_string(&*root, "email").value_or("");
    return session;
}

bool save_session(const SessionData& session, const std::filesystem::path& app_data_directory) {
    if (!ensure_app_data_directories(app_data_directory)) {
        return false;
    }

    JsonObject root {
        { "version", JsonValue { static_cast<double>(kPersistenceVersion) } },
        { "access_token", JsonValue { session.access_token } },
        { "refresh_token", JsonValue { session.refresh_token } },
        { "email", JsonValue { session.email } },
    };
    return save_json_text_atomic(session_file_path(app_data_directory), serialize_json(root));
}

UiStateData load_ui_state(const std::filesystem::path& app_data_directory) {
    ensure_app_data_directories(app_data_directory);
    auto json_text = load_json_text(ui_state_file_path(app_data_directory));
    if (!json_text) {
        return default_ui_state();
    }

    auto root = parse_json_object(*json_text, ui_state_file_path(app_data_directory));
    if (!root) {
        return default_ui_state();
    }
    if (!has_supported_version(*root, ui_state_file_path(app_data_directory))) {
        return default_ui_state();
    }

    UiStateData ui_state;
    ui_state.last_tree_id = get_int(&*root, "last_tree_id").value_or(-1);
    return ui_state;
}

bool save_ui_state(const UiStateData& ui_state, const std::filesystem::path& app_data_directory) {
    if (!ensure_app_data_directories(app_data_directory)) {
        return false;
    }

    JsonObject root {
        { "version", JsonValue { static_cast<double>(kPersistenceVersion) } },
        { "last_tree_id", JsonValue { static_cast<double>(ui_state.last_tree_id) } },
    };
    return save_json_text_atomic(ui_state_file_path(app_data_directory), serialize_json(root));
}

TreeLayoutData load_tree_layout(int tree_id, const std::filesystem::path& app_data_directory) {
    ensure_app_data_directories(app_data_directory);
    auto json_text = load_json_text(tree_layout_file_path(app_data_directory, tree_id));
    if (!json_text) {
        return default_tree_layout();
    }

    auto root = parse_json_object(*json_text, tree_layout_file_path(app_data_directory, tree_id));
    if (!root) {
        return default_tree_layout();
    }
    if (!has_supported_version(*root, tree_layout_file_path(app_data_directory, tree_id))) {
        return default_tree_layout();
    }

    TreeLayoutData layout;

    if (const JsonObject* canvas = as_object(find_member(&*root, "canvas"))) {
        layout.canvas.zoom = get_number(canvas, "zoom").value_or(1.0);
        layout.canvas.offset_x = get_number(canvas, "offset_x").value_or(0.0);
        layout.canvas.offset_y = get_number(canvas, "offset_y").value_or(0.0);
    }

    if (const JsonObject* persons = as_object(find_member(&*root, "persons"))) {
        for (const auto& [person_id_text, position_value] : *persons) {
            const JsonObject* position = as_object(&position_value);
            if (!position) {
                continue;
            }

            int person_id = 0;
            const auto [ptr, error] = std::from_chars(person_id_text.data(), person_id_text.data() + person_id_text.size(), person_id);
            if (error != std::errc() || ptr != person_id_text.data() + person_id_text.size()) {
                continue;
            }

            layout.persons[person_id] = PersonLayoutPosition {
                .x = get_number(position, "x").value_or(0.0),
                .y = get_number(position, "y").value_or(0.0),
            };
        }
    }

    // Parse relationships if present. Stored as object keyed by relationship id.
    if (const JsonObject* relationships = as_object(find_member(&*root, "relationships"))) {
        for (const auto& [rel_id_text, rel_value] : *relationships) {
            const JsonObject* rel_obj = as_object(&rel_value);
            if (!rel_obj) {
                continue;
            }

            int rel_id = 0;
            const auto [ptr2, err2] = std::from_chars(rel_id_text.data(), rel_id_text.data() + rel_id_text.size(), rel_id);
            if (err2 != std::errc() || ptr2 != rel_id_text.data() + rel_id_text.size()) {
                continue;
            }

            auto from_id = get_int(rel_obj, "from");
            auto to_id = get_int(rel_obj, "to");
            auto type_str = get_string(rel_obj, "type");
            if (!from_id || !to_id || !type_str) {
                continue;
            }

            RelationshipType rt = RelationshipType::Parent;
            if (*type_str == "parent") rt = RelationshipType::Parent;
            else if (*type_str == "spouse") rt = RelationshipType::Spouse;
            else if (*type_str == "sibling") rt = RelationshipType::Sibling;
            else rt = RelationshipType::Friend;

            layout.relationships.emplace_back(Relationship{
                .id = rel_id,
                .from_person_id = *from_id,
                .to_person_id = *to_id,
                .relationship_type = rt,
            });
        }
    }

    return layout;
}

bool save_tree_layout(int tree_id, const TreeLayoutData& tree_layout, const std::filesystem::path& app_data_directory) {
    if (!ensure_app_data_directories(app_data_directory)) {
        return false;
    }

    JsonObject canvas {
        { "zoom", JsonValue { tree_layout.canvas.zoom } },
        { "offset_x", JsonValue { tree_layout.canvas.offset_x } },
        { "offset_y", JsonValue { tree_layout.canvas.offset_y } },
    };

    JsonObject persons;
    for (const auto& [person_id, position] : tree_layout.persons) {
        persons.emplace(
            std::to_string(person_id),
            JsonValue { JsonObject {
                { "x", JsonValue { position.x } },
                { "y", JsonValue { position.y } },
            } }
        );
    }

    // Serialize relationships as object keyed by relationship id
    JsonObject relationships;
    for (const auto &rel : tree_layout.relationships) {
        std::string type_str = "parent";
        switch (rel.relationship_type) {
            case RelationshipType::Parent: type_str = "parent"; break;
            case RelationshipType::Spouse: type_str = "spouse"; break;
            case RelationshipType::Sibling: type_str = "sibling"; break;
            case RelationshipType::Friend: type_str = "friend"; break;
        }

        relationships.emplace(
            std::to_string(rel.id),
            JsonValue { JsonObject {
                { "from", JsonValue { static_cast<double>(rel.from_person_id) } },
                { "to", JsonValue { static_cast<double>(rel.to_person_id) } },
                { "type", JsonValue { type_str } },
            } }
        );
    }

    JsonObject root {
        { "version", JsonValue { static_cast<double>(kPersistenceVersion) } },
        { "canvas", JsonValue { canvas } },
        { "persons", JsonValue { persons } },
        { "relationships", JsonValue { relationships } },
    };

    return save_json_text_atomic(tree_layout_file_path(app_data_directory, tree_id), serialize_json(root));
}

} // namespace storage
