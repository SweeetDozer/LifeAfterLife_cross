#include "tree_api.h"

#include <algorithm>

namespace api {

namespace {

std::string escape_json(std::string_view value)
{
    std::string escaped;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
        case '"':
            escaped.push_back('\\');
            escaped.push_back(ch);
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

ApiError missing_field_error(std::string field)
{
    return ApiError {
        .type = ApiErrorType::Parse,
        .http_status = 200,
        .message = "Missing field in tree response: " + field,
    };
}

void append_json_string_field(std::string& json, std::string_view key, std::string_view value, bool& first_field)
{
    if (!first_field) {
        json += ",";
    }
    first_field = false;
    json += "\"";
    json += key;
    json += "\":\"";
    json += escape_json(value);
    json += "\"";
}

void append_json_bool_field(std::string& json, std::string_view key, bool value, bool& first_field)
{
    if (!first_field) {
        json += ",";
    }
    first_field = false;
    json += "\"";
    json += key;
    json += "\":";
    json += value ? "true" : "false";
}

void append_json_int_field(std::string& json, std::string_view key, int value, bool& first_field)
{
    if (!first_field) {
        json += ",";
    }
    first_field = false;
    json += "\"";
    json += key;
    json += "\":";
    json += std::to_string(value);
}

void append_json_nullable_string_field(std::string& json,
                                       std::string_view key,
                                       const std::optional<std::optional<std::string>>& value,
                                       bool& first_field)
{
    if (!value.has_value()) {
        return;
    }

    if (!first_field) {
        json += ",";
    }
    first_field = false;
    json += "\"";
    json += key;
    json += "\":";
    if (value->has_value()) {
        json += "\"";
        json += escape_json(**value);
        json += "\"";
    } else {
        json += "null";
    }
}

} // namespace

TreeApi::TreeApi(ApiClient& api_client)
    : api_client_(api_client)
{
}

ApiResult<std::vector<TreeSummary>> TreeApi::fetch_tree_list()
{
    auto response = api_client_.get_json("/trees/");
    if (!response.ok) {
        return ApiResult<std::vector<TreeSummary>>::failure(*response.error);
    }

    const JsonArray* array = as_array(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!array) {
        return ApiResult<std::vector<TreeSummary>>::failure(missing_field_error("tree list array"));
    }

    std::vector<TreeSummary> trees;
    trees.reserve(array->size());
    for (const auto& entry : *array) {
        auto tree_result = parse_tree_summary(entry);
        if (!tree_result.ok) {
            return ApiResult<std::vector<TreeSummary>>::failure(*tree_result.error);
        }
        trees.push_back(std::move(tree_result.value));
    }

    return ApiResult<std::vector<TreeSummary>>::success(std::move(trees));
}

ApiResult<TreeSummary> TreeApi::fetch_tree_data(int tree_id)
{
    auto tree_list_result = fetch_tree_list();
    if (!tree_list_result.ok) {
        return ApiResult<TreeSummary>::failure(*tree_list_result.error);
    }

    const auto it = std::find_if(tree_list_result.value.begin(), tree_list_result.value.end(), [tree_id](const TreeSummary& tree) {
        return tree.id == tree_id;
    });
    if (it == tree_list_result.value.end()) {
        return ApiResult<TreeSummary>::failure(ApiError {
            .type = ApiErrorType::Http,
            .http_status = 404,
            .message = "Tree not found in tree list response.",
        });
    }

    return ApiResult<TreeSummary>::success(*it);
}

ApiResult<std::vector<PersonRecord>> TreeApi::fetch_persons(int tree_id)
{
    auto response = api_client_.get_json("/persons/tree/" + std::to_string(tree_id));
    if (!response.ok) {
        return ApiResult<std::vector<PersonRecord>>::failure(*response.error);
    }

    const JsonArray* array = as_array(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!array) {
        return ApiResult<std::vector<PersonRecord>>::failure(missing_field_error("person list array"));
    }

    std::vector<PersonRecord> persons;
    persons.reserve(array->size());
    for (const auto& entry : *array) {
        auto person_result = parse_person_record(entry);
        if (!person_result.ok) {
            return ApiResult<std::vector<PersonRecord>>::failure(*person_result.error);
        }
        persons.push_back(std::move(person_result.value));
    }

    return ApiResult<std::vector<PersonRecord>>::success(std::move(persons));
}

ApiResult<PersonCreateResult> TreeApi::create_person(int tree_id, const PersonMutationData& person)
{
    if (!person.first_name.has_value() || !person.first_name->has_value()) {
        return ApiResult<PersonCreateResult>::failure(missing_field_error("first_name"));
    }

    bool first_field = true;
    std::string request_body = "{";
    append_json_nullable_string_field(request_body, "first_name", person.first_name, first_field);
    append_json_int_field(request_body, "tree_id", tree_id, first_field);
    append_json_nullable_string_field(request_body, "middle_name", person.middle_name, first_field);
    append_json_nullable_string_field(request_body, "last_name", person.last_name, first_field);
    append_json_nullable_string_field(request_body, "birth_date", person.birth_date, first_field);
    append_json_nullable_string_field(request_body, "death_date", person.death_date, first_field);
    append_json_nullable_string_field(request_body, "description", person.description, first_field);
    request_body += "}";

    auto response = api_client_.post_json("/persons/", request_body);
    if (!response.ok) {
        return ApiResult<PersonCreateResult>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<PersonCreateResult>::failure(missing_field_error("create person response object"));
    }

    auto person_id = json_get_int(object, "person_id");
    if (!person_id) {
        return ApiResult<PersonCreateResult>::failure(missing_field_error("person_id"));
    }

    return ApiResult<PersonCreateResult>::success(PersonCreateResult {
        .person_id = *person_id,
    });
}

ApiResult<PersonRecord> TreeApi::update_person(int person_id, const PersonMutationData& person)
{
    bool first_field = true;
    std::string request_body = "{";
    append_json_nullable_string_field(request_body, "first_name", person.first_name, first_field);
    append_json_nullable_string_field(request_body, "middle_name", person.middle_name, first_field);
    append_json_nullable_string_field(request_body, "last_name", person.last_name, first_field);
    append_json_nullable_string_field(request_body, "birth_date", person.birth_date, first_field);
    append_json_nullable_string_field(request_body, "death_date", person.death_date, first_field);
    append_json_nullable_string_field(request_body, "description", person.description, first_field);
    request_body += "}";

    if (first_field) {
        return ApiResult<PersonRecord>::failure(ApiError {
            .type = ApiErrorType::Parse,
            .http_status = 0,
            .message = "No person fields specified for update.",
        });
    }

    auto response = api_client_.patch_json("/persons/" + std::to_string(person_id), request_body);
    if (!response.ok) {
        return ApiResult<PersonRecord>::failure(*response.error);
    }

    const JsonValue* value = response.value.json_body ? &*response.value.json_body : nullptr;
    if (!value) {
        return ApiResult<PersonRecord>::failure(missing_field_error("update person response body"));
    }

    return parse_person_record(*value);
}

ApiResult<PersonDeleteResult> TreeApi::delete_person(int person_id)
{
    auto response = api_client_.delete_json("/persons/" + std::to_string(person_id));
    if (!response.ok) {
        return ApiResult<PersonDeleteResult>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<PersonDeleteResult>::failure(missing_field_error("delete person response object"));
    }

    auto detail = json_get_string(object, "detail");
    auto deleted_relationships = json_get_int(object, "deleted_relationships");
    if (!detail) {
        return ApiResult<PersonDeleteResult>::failure(missing_field_error("detail"));
    }
    if (!deleted_relationships) {
        return ApiResult<PersonDeleteResult>::failure(missing_field_error("deleted_relationships"));
    }

    return ApiResult<PersonDeleteResult>::success(PersonDeleteResult {
        .detail = *detail,
        .deleted_relationships = *deleted_relationships,
    });
}

ApiResult<RelationshipCreateResult> TreeApi::create_relationship(int from_person_id,
                                                                 int to_person_id,
                                                                 const std::string& relationship_type)
{
    bool first_field = true;
    std::string request_body = "{";
    append_json_int_field(request_body, "from_person_id", from_person_id, first_field);
    append_json_int_field(request_body, "to_person_id", to_person_id, first_field);
    append_json_string_field(request_body, "relationship_type", relationship_type, first_field);
    request_body += "}";

    auto response = api_client_.post_json("/relationships/", request_body);
    if (!response.ok) {
        return ApiResult<RelationshipCreateResult>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<RelationshipCreateResult>::failure(missing_field_error("create relationship response object"));
    }

    auto relationship_id = json_get_int(object, "relationship_id");
    if (!relationship_id) {
        return ApiResult<RelationshipCreateResult>::failure(missing_field_error("relationship_id"));
    }

    return ApiResult<RelationshipCreateResult>::success(RelationshipCreateResult {
        .relationship_id = *relationship_id,
    });
}

ApiResult<RelationshipDeleteResult> TreeApi::delete_relationship(int relationship_id)
{
    auto response = api_client_.delete_json("/relationships/" + std::to_string(relationship_id));
    if (!response.ok) {
        return ApiResult<RelationshipDeleteResult>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<RelationshipDeleteResult>::failure(missing_field_error("delete relationship response object"));
    }

    auto detail = json_get_string(object, "detail");
    auto deleted_relationships = json_get_int(object, "deleted_relationships");
    if (!detail) {
        return ApiResult<RelationshipDeleteResult>::failure(missing_field_error("detail"));
    }
    if (!deleted_relationships) {
        return ApiResult<RelationshipDeleteResult>::failure(missing_field_error("deleted_relationships"));
    }

    return ApiResult<RelationshipDeleteResult>::success(RelationshipDeleteResult {
        .detail = *detail,
        .deleted_relationships = *deleted_relationships,
    });
}

ApiResult<TreeCreateResult> TreeApi::create_tree(const std::string& name,
                                                 const std::optional<std::string>& description,
                                                 bool is_public)
{
    bool first_field = true;
    std::string request_body = "{";
    append_json_string_field(request_body, "name", name, first_field);
    append_json_bool_field(request_body, "is_public", is_public, first_field);
    if (description.has_value()) {
        append_json_string_field(request_body, "description", *description, first_field);
    }
    request_body += "}";

    auto response = api_client_.post_json("/trees/", request_body);
    if (!response.ok) {
        return ApiResult<TreeCreateResult>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<TreeCreateResult>::failure(missing_field_error("create tree response object"));
    }

    auto tree_id = json_get_int(object, "tree_id");
    if (!tree_id) {
        return ApiResult<TreeCreateResult>::failure(missing_field_error("tree_id"));
    }

    return ApiResult<TreeCreateResult>::success(TreeCreateResult {
        .tree_id = *tree_id,
    });
}

ApiResult<TreeSummary> TreeApi::update_tree(int tree_id,
                                           const std::optional<std::string>& name,
                                           const std::optional<std::string>& description,
                                           const std::optional<bool>& is_public)
{
    bool first_field = true;
    std::string request_body = "{";
    if (name.has_value()) {
        append_json_string_field(request_body, "name", *name, first_field);
    }
    if (description.has_value()) {
        append_json_string_field(request_body, "description", *description, first_field);
    }
    if (is_public.has_value()) {
        append_json_bool_field(request_body, "is_public", *is_public, first_field);
    }
    request_body += "}";

    if (first_field) {
        return ApiResult<TreeSummary>::failure(ApiError {
            .type = ApiErrorType::Parse,
            .http_status = 0,
            .message = "No tree fields specified for update.",
        });
    }

    auto response = api_client_.patch_json("/trees/" + std::to_string(tree_id), request_body);
    if (!response.ok) {
        return ApiResult<TreeSummary>::failure(*response.error);
    }

    const JsonValue* value = response.value.json_body ? &*response.value.json_body : nullptr;
    if (!value) {
        return ApiResult<TreeSummary>::failure(missing_field_error("update tree response body"));
    }

    return parse_tree_summary(*value);
}

ApiResult<TreeDeleteResult> TreeApi::delete_tree(int tree_id)
{
    auto response = api_client_.delete_json("/trees/" + std::to_string(tree_id));
    if (!response.ok) {
        return ApiResult<TreeDeleteResult>::failure(*response.error);
    }

    const JsonObject* object = as_object(response.value.json_body ? &*response.value.json_body : nullptr);
    if (!object) {
        return ApiResult<TreeDeleteResult>::failure(missing_field_error("delete tree response object"));
    }

    auto detail = json_get_string(object, "detail");
    auto deleted_persons = json_get_int(object, "deleted_persons");
    auto deleted_relationships = json_get_int(object, "deleted_relationships");
    auto deleted_access_entries = json_get_int(object, "deleted_access_entries");
    if (!detail) {
        return ApiResult<TreeDeleteResult>::failure(missing_field_error("detail"));
    }
    if (!deleted_persons) {
        return ApiResult<TreeDeleteResult>::failure(missing_field_error("deleted_persons"));
    }
    if (!deleted_relationships) {
        return ApiResult<TreeDeleteResult>::failure(missing_field_error("deleted_relationships"));
    }
    if (!deleted_access_entries) {
        return ApiResult<TreeDeleteResult>::failure(missing_field_error("deleted_access_entries"));
    }

    return ApiResult<TreeDeleteResult>::success(TreeDeleteResult {
        .detail = *detail,
        .deleted_persons = *deleted_persons,
        .deleted_relationships = *deleted_relationships,
        .deleted_access_entries = *deleted_access_entries,
    });
}

ApiResult<TreeSummary> TreeApi::parse_tree_summary(const JsonValue& json_value) const
{
    const JsonObject* object = as_object(&json_value);
    if (!object) {
        return ApiResult<TreeSummary>::failure(missing_field_error("tree object"));
    }

    auto id = json_get_int(object, "id");
    auto name = json_get_string(object, "name");
    auto created_at = json_get_string(object, "created_at");
    auto access_level = json_get_string(object, "access_level");
    if (!id) {
        return ApiResult<TreeSummary>::failure(missing_field_error("id"));
    }
    if (!name) {
        return ApiResult<TreeSummary>::failure(missing_field_error("name"));
    }
    if (!created_at) {
        return ApiResult<TreeSummary>::failure(missing_field_error("created_at"));
    }
    if (!access_level) {
        return ApiResult<TreeSummary>::failure(missing_field_error("access_level"));
    }

    TreeSummary summary;
    summary.id = *id;
    summary.name = *name;
    summary.description = json_get_string(object, "description").value_or("");
    summary.is_public = json_get_bool(object, "is_public").value_or(false);
    summary.owner_id = json_get_int(object, "owner_id");
    summary.created_at = *created_at;
    summary.access_level = *access_level;
    return ApiResult<TreeSummary>::success(std::move(summary));
}

ApiResult<PersonRecord> TreeApi::parse_person_record(const JsonValue& json_value) const
{
    const JsonObject* object = as_object(&json_value);
    if (!object) {
        return ApiResult<PersonRecord>::failure(missing_field_error("person object"));
    }

    auto id = json_get_int(object, "id");
    auto tree_id = json_get_int(object, "tree_id");
    auto first_name = json_get_string(object, "first_name");
    if (!id) {
        return ApiResult<PersonRecord>::failure(missing_field_error("id"));
    }
    if (!tree_id) {
        return ApiResult<PersonRecord>::failure(missing_field_error("tree_id"));
    }
    if (!first_name) {
        return ApiResult<PersonRecord>::failure(missing_field_error("first_name"));
    }

    PersonRecord person;
    person.id = *id;
    person.tree_id = *tree_id;
    person.first_name = *first_name;
    person.middle_name = json_get_string(object, "middle_name").value_or("");
    person.last_name = json_get_string(object, "last_name").value_or("");
    person.birth_date = json_get_string(object, "birth_date").value_or("");
    person.death_date = json_get_string(object, "death_date").value_or("");
    person.description = json_get_string(object, "description").value_or("");
    return ApiResult<PersonRecord>::success(std::move(person));
}

} // namespace api
