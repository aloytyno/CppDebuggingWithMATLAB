| Date | Skill | Worked Well | Unexpected | Do Differently |
|---|---|---|---|---|
| 2026-11-04 | demo-guidelines | Persistent figures and concise stage labels make the data handoffs easy to present. | The Engine runtime needs both MATLAB runtime folders on PATH for external C++ processes. | Start future Engine demos with a RelWithDebInfo build to retain VS Code symbols without debug-CRT dependencies. |
| 2026-11-04 | demo-review-rubric | The C++ handoff markers give reviewers and presenters concrete debugger locations. | The standalone Data API self-test cannot construct arrays before connecting to an Engine session. | Keep engine-independent checks in standard C++ containers and reserve Data API validation for the integrated demo. |
