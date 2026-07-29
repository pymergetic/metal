# Definitions — async ranks

Precise wording for the coro hierarchy. Promotion only adds stamps;
await/frame mechanics stay the coro.

| Term | Is | Rank addition |
|------|----|----------------|
| [coro](coro.md) | one activation (step + frame); nestable | park / nest (not schedule) |
| [task](task.md) | that coro on a runner | + schedule |
| [process](process.md) | that task as orchestration face | + intent flag + watched id |

**Nest vs schedule:** coros may `await` each other with no queue entry;
only the outer task is scheduled. See the relation sections in
[coro](coro.md) and [task](task.md).

Orthogonal (not defined here): module code mode (PURE / SESSION / APP
cages).
