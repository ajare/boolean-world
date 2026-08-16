# VertexTransformer
A VertexTransformer is an object which does 90% of the heavy lifting of transforming Primitives.  It should
maybe be better called PrimitiveTransformer but it works at the vertex level.

The VertexTransformer tracks 4 properties, which combined together transform a vertex's position in a typical
way:

- Scale.  This is a value in [1, 10] where the number is a multiple of the Primitive's *size* parameter.
- Angle.  This is a value in [0, 360]
- Orbit Angle.  This is a value in [0, 360]
- Orbit Distance.  This is a value greater than zero
It receives input values from the world, which act as its primary parameters.  These are:

- Distance from entity (player) to the "influence eye" of the Primitive.
- Angle (from 0 degrees anticlockwise) from the Primitive to the entity.
- Angle of the entity (independent of the Primitive).
For each of the properties, the VertexTransformer has an *AnimatedProperty*.
## AnimatedProperty
These hold four things:
- A set of transforms known as a *TransformFlow*.  These take one or more inputs and combine them
together to produce a value in [0, 1].
- An Interpolator which interpolates keyframes for the property.
- An Interpolator which interpolates keyframes for influence amount.
- A *ValueCapture* instance, which allows for the property interpolation to behave in different ways.
The influence amount is a value from zero to the maximum influence distance and converts to a value
in [0, 1] which acts as a multiplier to the property interpolation.
## FollowOrbitAngle
This tells the primitive to angle itself so that it retains its orientation with respect to its orbit centre,
as it rotates around the orbit origin.  This is applied on top of any other rotation on the Primitive.
## Vertex Transformation Process
Each time a Primitive changes, its vertices are recalculated.  First, inputs are calculated as follows:
## Vertex transformation process
A vertex is transformed from its position in unit space as follows:
1. For each of the four properties - scale, angle, orbit angle and orbit distance - the relevant TransformFlow
is calculated based on any input variables used.
2. The resultant values in [0, 1] are passed into the ValueCapture mechanism, which clamps the value as appropriate,
still in [0, 1]
3. This value is the fed into the property interpolator, which produces a value in the relevant domain (ie distance
or angle.
4. Finally, the distance from the entity to the influence eye is passed into the influence interpolator, which produces
a value in [0, 1] which acts as a modulator/multiplier for the value from 3).
5. Once the 4 values are calculated, they are combined together to create a world transform for the vertex.

