#pragma once

#include "app-window.h"
#include "../models/person.h"
#include "../models/relationship.h"

#include <vector>

void sync_selected_person_details(const AppWindow &app, const Person &person);
void sync_inspector_draft(const AppWindow &app, const Person *person);
void sync_selected_person(const AppWindow &app, const std::vector<Person> &persons, int selected_person_id);
void clear_selected_person(const AppWindow &app, bool clear_draft = true);
void clear_selected_relationship(const AppWindow &app);
void sync_selected_relationship(const AppWindow &app,
                                const std::vector<Person> &persons,
                                const std::vector<Relationship> &relationships,
                                int selected_relationship_id);
