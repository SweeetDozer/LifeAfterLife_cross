#pragma once

#include "api_client.h"

#include <optional>
#include <string>
#include <vector>

namespace api {

struct TreeSummary {
    int id = -1;
    std::string name;
    std::string description;
    bool is_public = false;
    std::optional<int> owner_id;
    std::string created_at;
    std::string access_level;
};

struct TreeCreateResult {
    int tree_id = -1;
};

struct TreeDeleteResult {
    std::string detail;
    int deleted_persons = 0;
    int deleted_relationships = 0;
    int deleted_access_entries = 0;
};

struct PersonRecord {
    int id = -1;
    int tree_id = -1;
    std::string first_name;
    std::string middle_name;
    std::string last_name;
    std::string birth_date;
    std::string death_date;
    std::string description;
};

template<typename T>
using OptionalNullable = std::optional<std::optional<T>>;

struct PersonMutationData {
    OptionalNullable<std::string> first_name;
    OptionalNullable<std::string> middle_name;
    OptionalNullable<std::string> last_name;
    OptionalNullable<std::string> birth_date;
    OptionalNullable<std::string> death_date;
    OptionalNullable<std::string> description;
};

struct PersonCreateResult {
    int person_id = -1;
};

struct PersonDeleteResult {
    std::string detail;
    int deleted_relationships = 0;
};

struct RelationshipCreateResult {
    int relationship_id = -1;
};

struct RelationshipDeleteResult {
    std::string detail;
    int deleted_relationships = 0;
};

class TreeApi {
public:
    explicit TreeApi(ApiClient& api_client);

    ApiResult<std::vector<TreeSummary>> fetch_tree_list();
    ApiResult<TreeSummary> fetch_tree_data(int tree_id);
    ApiResult<std::vector<PersonRecord>> fetch_persons(int tree_id);
    ApiResult<PersonCreateResult> create_person(int tree_id, const PersonMutationData& person);
    ApiResult<PersonRecord> update_person(int person_id, const PersonMutationData& person);
    ApiResult<PersonDeleteResult> delete_person(int person_id);
    ApiResult<RelationshipCreateResult> create_relationship(int from_person_id,
                                                            int to_person_id,
                                                            const std::string& relationship_type);
    ApiResult<RelationshipDeleteResult> delete_relationship(int relationship_id);
    ApiResult<TreeCreateResult> create_tree(const std::string& name,
                                            const std::optional<std::string>& description = std::nullopt,
                                            bool is_public = false);
    ApiResult<TreeSummary> update_tree(int tree_id,
                                       const std::optional<std::string>& name = std::nullopt,
                                       const std::optional<std::string>& description = std::nullopt,
                                       const std::optional<bool>& is_public = std::nullopt);
    ApiResult<TreeDeleteResult> delete_tree(int tree_id);

private:
    ApiResult<TreeSummary> parse_tree_summary(const JsonValue& json_value) const;
    ApiResult<PersonRecord> parse_person_record(const JsonValue& json_value) const;

    ApiClient& api_client_;
};

} // namespace api
