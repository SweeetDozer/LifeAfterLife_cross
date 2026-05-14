#pragma once

enum class RelationshipType {
    Parent,
    Spouse,
    Sibling,
    Friend,
};

struct Relationship {
    int id;
    int from_person_id;
    int to_person_id;
    RelationshipType relationship_type;
};
