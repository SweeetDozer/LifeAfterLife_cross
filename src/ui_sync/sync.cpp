#include "sync.h"

#include <algorithm>

void sync_selected_person_details(const AppWindow &app, const Person &person)
{
    app.set_selected_person_first_name(slint::SharedString(person.first_name));
    app.set_selected_person_middle_name(slint::SharedString(person.middle_name));
    app.set_selected_person_last_name(slint::SharedString(person.last_name));
    app.set_selected_person_birth_date(slint::SharedString(person.birth_date));
    app.set_selected_person_death_date(slint::SharedString(person.death_date));
}

void sync_inspector_draft(const AppWindow &app, const Person *person)
{
    if (!person) {
        app.set_inspector_draft_first_name(slint::SharedString());
        app.set_inspector_draft_middle_name(slint::SharedString());
        app.set_inspector_draft_last_name(slint::SharedString());
        app.set_inspector_draft_birth_date(slint::SharedString());
        app.set_inspector_draft_death_date(slint::SharedString());
        return;
    }

    app.set_inspector_draft_first_name(slint::SharedString(person->first_name));
    app.set_inspector_draft_middle_name(slint::SharedString(person->middle_name));
    app.set_inspector_draft_last_name(slint::SharedString(person->last_name));
    app.set_inspector_draft_birth_date(slint::SharedString(person->birth_date));
    app.set_inspector_draft_death_date(slint::SharedString(person->death_date));
}

void sync_selected_person(const AppWindow &app, const std::vector<Person> &persons, int selected_person_id)
{
    app.set_selected_person_id(selected_person_id);

    for (const auto &person : persons) {
        if (person.id == selected_person_id) {
            sync_selected_person_details(app, person);
            return;
        }
    }

    app.set_selected_person_first_name(slint::SharedString());
    app.set_selected_person_middle_name(slint::SharedString());
    app.set_selected_person_last_name(slint::SharedString());
    app.set_selected_person_birth_date(slint::SharedString());
    app.set_selected_person_death_date(slint::SharedString());
}

void clear_selected_person(const AppWindow &app, bool clear_draft)
{
    app.set_selected_person_id(-1);
    app.set_selected_person_first_name(slint::SharedString());
    app.set_selected_person_middle_name(slint::SharedString());
    app.set_selected_person_last_name(slint::SharedString());
    app.set_selected_person_birth_date(slint::SharedString());
    app.set_selected_person_death_date(slint::SharedString());
    if (clear_draft) {
        sync_inspector_draft(app, nullptr);
    }
}

void clear_selected_relationship(const AppWindow &app)
{
    app.set_selected_relationship_id(-1);
    app.set_selected_relationship_type(slint::SharedString());
    app.set_selected_relationship_from_first_name(slint::SharedString());
    app.set_selected_relationship_from_middle_name(slint::SharedString());
    app.set_selected_relationship_from_last_name(slint::SharedString());
    app.set_selected_relationship_to_first_name(slint::SharedString());
    app.set_selected_relationship_to_middle_name(slint::SharedString());
    app.set_selected_relationship_to_last_name(slint::SharedString());
}

void sync_selected_relationship(const AppWindow &app,
                                const std::vector<Person> &persons,
                                const std::vector<Relationship> &relationships,
                                int selected_relationship_id)
{
    app.set_selected_relationship_id(selected_relationship_id);

    const auto relationship_it = std::find_if(relationships.begin(), relationships.end(), [selected_relationship_id](const Relationship &relationship) {
        return relationship.id == selected_relationship_id;
    });

    if (relationship_it == relationships.end()) {
        clear_selected_relationship(app);
        return;
    }

    const auto from_it = std::find_if(persons.begin(), persons.end(), [relationship_it](const Person &person) {
        return person.id == relationship_it->from_person_id;
    });
    const auto to_it = std::find_if(persons.begin(), persons.end(), [relationship_it](const Person &person) {
        return person.id == relationship_it->to_person_id;
    });

    const auto relationship_type =
        relationship_it->relationship_type == RelationshipType::Parent ? "parent"
        : relationship_it->relationship_type == RelationshipType::Spouse ? "spouse"
        : relationship_it->relationship_type == RelationshipType::Sibling ? "sibling"
        : "friend";

    app.set_selected_relationship_type(slint::SharedString(relationship_type));
    app.set_selected_relationship_from_first_name(from_it == persons.end() ? slint::SharedString() : slint::SharedString(from_it->first_name));
    app.set_selected_relationship_from_middle_name(from_it == persons.end() ? slint::SharedString() : slint::SharedString(from_it->middle_name));
    app.set_selected_relationship_from_last_name(from_it == persons.end() ? slint::SharedString() : slint::SharedString(from_it->last_name));
    app.set_selected_relationship_to_first_name(to_it == persons.end() ? slint::SharedString() : slint::SharedString(to_it->first_name));
    app.set_selected_relationship_to_middle_name(to_it == persons.end() ? slint::SharedString() : slint::SharedString(to_it->middle_name));
    app.set_selected_relationship_to_last_name(to_it == persons.end() ? slint::SharedString() : slint::SharedString(to_it->last_name));
}
