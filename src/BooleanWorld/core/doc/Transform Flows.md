# Transform Flow
A Transform Flow is an ordered list of Transforms, applied one after the other, and potentially using the result of the previous Transform.

A Transform is essentially a binary operation, with two operands and an operator.  The operands can be of the following type:

- A global input
- A constant value.  Constant values must be in [0, 1]
- The result of the previous transform (if used in the first transform, treated as zero)

This lets you combine different inputs in different ways.  The list of supported operators is:

- Add
- Multiply
- Absolute difference between the values
- Minimum value
- Maximum value
- Average value
- Less than (returns zero or one)
- Greater than (returns zero or one)
- Less than or equal (returns zero or one)
- Greater than or equal (returns zero or one)

As only binary operations are supported, to pass a single value or input through with no further modification, you will need to something
like ```<value> Add 0``` or ```<value> Mul 1```.  Or even ```<value> Avg <value>``` etc, although of course adding/multiplying is more efficient.
## Output
Output values are clamped to [0, 1] and passed into an animation Interpolator in the VertexTransformer.
