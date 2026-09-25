# ECS

A small C++ Entity-Component-System (ECS) implementation.

The ECS provides:

* Entity creation and destruction
* Entity IDs containing an index and version
* Reuse of destroyed entity slots
* Per-component-type storage
* Dense component storage with sparse entity-to-component lookup
* Adding, retrieving, and removing components
* Iteration over entities matching one or more component types through `SceneView`

## Contents

```text
ECS/
├── ComponentPool.cpp
├── ComponentPool.h
├── EntityID.h
└── EntityManager.h
```

No external dependencies are used by the files in this repository.

## Overview

The ECS separates an entity's identity from its components.

An `EntityID` identifies an entity, while components are stored in separate `ComponentPool<T>` instances based on their C++ type.

Conceptually:

```text
EntityManager
│
├── Entity IDs
│   ├── Entity 0
│   ├── Entity 1
│   └── ...
│
└── Component pools
    ├── ComponentPool<Position>
    ├── ComponentPool<Velocity>
    ├── ComponentPool<Health>
    └── ...
```

## Entity IDs

Entity IDs are represented by a 64-bit integer:

```cpp
using EntityIndex = std::uint32_t;
using EntityVersion = std::uint32_t;
using EntityID = std::uint64_t;
```

The 64-bit ID is composed of two 32-bit values:

```text
┌──────────────────────────────┬──────────────────────────────┐
│        Entity Index          │       Entity Version         │
│           32 bits            │           32 bits            │
└──────────────────────────────┴──────────────────────────────┘
```

The helper functions are:

```cpp
CreateEntityID(index, version);
GetEntityIndex(id);
GetEntityVersion(id);
IsEntityIDValid(id);
```

There is also an `INVALID_ENTITY` constant.

### Entity reuse

When an entity is destroyed, its index can be reused by a future entity.

The version is incremented when the entity is destroyed. This allows an old `EntityID` to become invalid even when its index is later reused.

For example:

```text
Create:
    index = 5
    version = 0

    EntityID = (5, 0)

Destroy:
    index = 5
    version = 1
    slot becomes invalid

Create another entity:
    index = 5
    version = 1

    EntityID = (5, 1)
```

An old `(5, 0)` ID therefore does not refer to the newly created entity.

## EntityManager

`EntityManager` owns the entity records and component pools.

### Creating an entity

```cpp
EntityManager entities;

EntityID entity = entities.CreateEntity();
```

If there are previously destroyed entity slots available, the manager reuses one of them. Otherwise, a new slot is appended.

### Destroying an entity

```cpp
entities.DestroyEntity(entity);
```

Destroying an entity:

1. Verifies that the entity is currently alive.
2. Invalidates its entity ID.
3. Increments its version.
4. Adds its index to the free-entity list.
5. Removes the entity from every existing component pool.

### Checking whether an entity is alive

```cpp
if (entities.IsEntityAlive(entity))
{
    // ...
}
```

The check compares the complete `EntityID`, including its version.

### Getting an entity by index

```cpp
EntityID entity = entities.GetEntityByIndex(index);
```

If the index is outside the entity array, `INVALID_ENTITY` is returned.

A destroyed entity's slot also contains an invalid entity ID.

### Entity count

```cpp
size_t count = entities.GetEntityCount();
```

`GetEntityCount()` returns the size of the entity storage, not necessarily the number of currently alive entities.

In particular, destroyed entity slots remain in `m_entities` so that their indices can be reused.

## Components

Components are ordinary C++ types.

For example:

```cpp
struct Position
{
    float x;
    float y;
};

struct Velocity
{
    float x;
    float y;
};
```

Components do not need to inherit from a base class.

### Adding a component

Components can be constructed directly in their pool:

```cpp
EntityID entity = entities.CreateEntity();

Position* position =
    entities.AddComponent<Position>(entity, 10.0f, 20.0f);
```

The arguments after the entity ID are forwarded to the component's constructor.

For example:

```cpp
struct Health
{
    explicit Health(int value)
        : value(value)
    {
    }

    int value;
};

entities.AddComponent<Health>(entity, 100);
```

If the entity is not alive, `AddComponent` returns `nullptr`.

### Adding an existing component

If the entity already has a component of the requested type, `ComponentPool::Add` returns the existing component instead of adding a second copy.

Therefore, an entity can have at most one component of a given C++ type.

### Getting a component

```cpp
Position* position = entities.GetComponent<Position>(entity);

if (position)
{
    // Component exists.
}
```

A `const` overload is also provided:

```cpp
const Position* position = static_cast<const EntityManager&>(entities).GetComponent<Position>(entity);
```

`GetComponent` returns `nullptr` when:

* the entity is not alive,
* the component type has no pool yet, or
* the entity does not contain that component.

### Removing a component

```cpp
entities.RemoveComponent<Position>(entity);
```

## ComponentPool

`ComponentPool<T>` stores all components of a particular type.

It uses:

* a dense `std::vector<T>` for component data,
* a sparse, paged lookup structure mapping entity indices to dense indices,
* a reverse mapping from dense indices to entity indices.

Conceptually:

```text
Sparse lookup

┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│  -  │  -  │  0  │  -  │  1  │  -  │  -  │  -  │  2  │
└─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
   0     1     2     3     4     5     6     7     8


Dense storage

┌─────┬─────┬─────┐
│  T  │  T  │  T  │
└─────┴─────┴─────┘
   0     1     2

denseToEntity

┌─────┬─────┬─────┐
│  2  │  4  │  8  │
└─────┴─────┴─────┘
```

The sparse lookup is divided into pages of:

```cpp
constexpr size_t ENTITY_PAGE_SIZE = 32;
```

### Dense removal

Removing a component does not leave a hole in the dense component array.

If the component being removed is not the last component, the last component is moved into its position and the mappings are updated.

This keeps component storage dense, but means that a pointer or reference to a component should not be assumed to remain valid after operations that can modify the corresponding pool.

## Component Type IDs

Each component type receives an integer type ID through:

```cpp
template <class T>
int GetComponentTypeId();
```

The first time a particular component type is requested, it receives a new ID.

`EntityManager` uses these IDs to locate the corresponding component pool.

For example:

```cpp
GetComponentTypeId<Position>();
GetComponentTypeId<Velocity>();
GetComponentTypeId<Health>();
```

The IDs are generated globally through `s_componentCounter`.

## SceneView

`SceneView` provides iteration over entities that contain a specified set of component types.

For example:

```cpp
SceneView<Position, Velocity> view(entities);

for (auto [entity, position, velocity] : view)
{
    position.x += velocity.x;
    position.y += velocity.y;
}
```

The iterator returns:

```cpp
std::tuple<EntityID, ComponentTypes&...>
```

For a two-component view:

```cpp
SceneView<Position, Velocity>
```

the result is effectively:

```cpp
std::tuple<EntityID, Position&, Velocity&>
```

### Multiple component types

A view only yields entities that contain **all** requested component types.

```cpp
SceneView<Position, Velocity, Health> view(entities);

for (auto [entity, position, velocity, health] : view)
{
    // Entity has Position, Velocity and Health.
}
```

### Iteration strategy

For a view containing components, the implementation selects the smallest component pool as the pool to iterate over.

For example:

```text
Position:  10,000 components
Velocity:   2,000 components
Health:       500 components
```

A:

```cpp
SceneView<Position, Velocity, Health>
```

will use the `Health` pool as its starting point and check whether each candidate entity is present in the other pools.

This avoids scanning the largest pool when a smaller pool can provide the candidates.

### Empty `SceneView`

The implementation also contains handling for:

```cpp
SceneView<> view(entities);
```

In this case the iterator walks the entity storage rather than a component pool.

The intended iteration result is:

```cpp
std::tuple<EntityID>
```

Only currently valid/alive entity slots are yielded.

## Example

A minimal usage example looks like this:

```cpp
#include "ECS/EntityManager.h"

struct Position
{
    float x;
    float y;
};

struct Velocity
{
    float x;
    float y;
};

int main()
{
    EntityManager entities;

    EntityID entity = entities.CreateEntity();

    entities.AddComponent<Position>(entity, Position{0.0f, 0.0f});
    entities.AddComponent<Velocity>(entity, Velocity{1.0f, 2.0f});

    if (Position* position = entities.GetComponent<Position>(entity))
    {
        position->x += 10.0f;
    }

    SceneView<Position, Velocity> view(entities);

    for (auto [id, position, velocity] : view)
    {
        position.x += velocity.x;
        position.y += velocity.y;
    }

    entities.RemoveComponent<Velocity>(entity);
    entities.DestroyEntity(entity);
}
```

## API Summary

### `EntityID.h`

| API                              | Description                              |
| -------------------------------- | ---------------------------------------- |
| `CreateEntityID(index, version)` | Creates an `EntityID`                    |
| `GetEntityIndex(id)`             | Extracts the entity index                |
| `GetEntityVersion(id)`           | Extracts the entity version              |
| `IsEntityIDValid(id)`            | Checks whether the ID is marked as valid |
| `INVALID_ENTITY`                 | Invalid entity constant                  |

### `EntityManager`

| API                            | Description                                          |
| ------------------------------ | ---------------------------------------------------- |
| `CreateEntity()`               | Creates or reuses an entity slot                     |
| `DestroyEntity(entity)`        | Destroys an alive entity                             |
| `IsEntityAlive(entity)`        | Checks the full entity ID against the current entity |
| `GetEntityCount()`             | Returns the number of allocated entity slots         |
| `GetEntityByIndex(index)`      | Gets the ID stored at an entity index                |
| `AddComponent<T>(entity, ...)` | Adds or retrieves a component                        |
| `GetComponent<T>(entity)`      | Gets a component                                     |
| `RemoveComponent<T>(entity)`   | Removes a component                                  |
| `GetComponentPool<T>()`        | Gets the component pool for a type                   |

### `ComponentPool<T>`

| API                          | Description                                |
| ---------------------------- | ------------------------------------------ |
| `Add(index, ...)`            | Adds or returns an existing component      |
| `Get(index)`                 | Gets a component by entity index           |
| `Remove(index)`              | Removes a component                        |
| `Contains(index)`            | Checks whether a component exists          |
| `GetSize()`                  | Gets the number of stored components       |
| `GetEntityIndex(denseIndex)` | Maps a dense index back to an entity index |

### `SceneView<ComponentTypes...>`

```cpp
SceneView<A, B, C> view(entityManager);
```

Iterates over entities containing all requested component types.

## Project Structure

```text
ECS/
├── ComponentPool.cpp   # Definition of the global component type counter
├── ComponentPool.h     # ComponentPool and component type ID implementation
├── EntityID.h          # Entity ID representation and helpers
└── EntityManager.h     # EntityManager and SceneView implementation
```

Most of the implementation is currently header-based because the ECS functionality is implemented through templates.

## Status

This project is a work in progress, and I plan to continue improving and expanding it over time.

## Acknowledgements / References

This project was developed while learning from documentation, tutorials, examples, and other educational resources.
