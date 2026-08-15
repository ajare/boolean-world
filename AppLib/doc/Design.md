# Willpower AppLib

This is a helper library to providegame systems built on Willpower and MPP.

## AnimationDatabase

This is a database which holds sprite-based animations.  Its purpose is to store AnimationSet resources, and provide
an easy way to retrieve and loop through frames.

## Battery

A simple manager class for beams, bullets, etc.

## Beams

### Beam

A simple struct holding a reference to an owning entity, a unique id and a type which refers to a predefined, hardcoded
beam type.

### BeamEmitter

A component for entities which are able to emit beams.  This holds an anchor (ie base position and angle) and a list
of beam ids.

### BeamData

Instance data for a **Beam**.  The difference between **BeamData** and **Beam** is that **Beam** is for internal variables which
the client should not care about, whereas *BeamData* holds variables which control the state of the beam at a given time.

### BeamInstance

A composite struct which holds **Beam**, **BeamData** and the owning **Entity**.  There is also an *alive* flag for internal use.

### BeamManager

Holds a **Battery** of **BeamInstance**, and updates each.

### BeamRenderDataProvider

Generates data used to render a **Beam**.

## Bullets

### Bullet

Simple struct holding typical bullet data.

### BulletManager

Holds a **Battery** of **Bullet**, and updates each.

### BulletRenderDataProvider

Generates data used to render a **Bullet**.

## Entities

### Entity

Has a unique id, a name, and a list of properties (with lookup) that apply to it.

### EntityProperties

### EntityFacade

This class holds **Entity** instances of a given type, the idea being that it allows for optimised rendering, as each EntityFacade has
its own MPP renderer and data provider.  When *createEntity()* is called, it passes the new instance in an **EntityHandler** which sets
up the instance properties.  The class also handles updating entities, again by delegating to the **EntityHandler**.

### EntityManager

This class is the main management interface for **Entity** instances, and holds all the required **EntityFacade** instances as well.  The
idea is that the class lets the user create an **EntityFacade** given a given set of Entity types, and then when you call *EntityManager::createEntity()*,
you pass in the Entity type, and it delegates to the corresponding **EntityFacade**.  The class also handles update and rendering by iterating over its
EntityFacades.

### EntityHandler

This class manages individual **Entity** instances, setting them up, destroying them, and updating them.  **EntityFacade** delegates to it for this.  It
also stores a registry of entity properties.

### EntityRenderDataProvider

TODO

### ProtoEntityResourceDefinitionFactory

A subclass of **wp::application::resourcesystem::ResourceDefinitionFactory**, this is used to create **ProtoEntity** instance definitions.

### ProtoEntityDefaultDefinitionFactory

A subclass of **ProtoEntityResourceDefinitionFactory**, this is used to create **ProtoEntity** instance definitions.

### PhysicalStats

### VisualStats

### BeamEmitter

## Game

### GameDefaultDefinitionFactory

### GameResourceDefinitionFactory

## Map

### MapDefaultDefinitionFactory

### MapResourceDefinitionFactory

### MapTiledDefinitionFactory

### MapGeometryObjectAttributes

### MapTransitionData

## Model

### ModelInstance

## ProtoEntity

This is a Willpower **Resource**, intended to be subclassed by an application.  It has a pointer to an **EntityHandler**,
which is used to 

### ProtoEntityDefaultDefinitionFactory

### ProtoEntityResourceDefinitionFactory

## State

### StateTransitionData

### StateController

### StatePlay

### ThreadableLoadState

#### StateLoad

#### StateMapLoad

#### StateMapTransition

#### StateMapUnload

#### StateUnload




