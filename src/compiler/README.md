# Quill-Hosted Compiler Stages

`lexer.qsc`, `parser.qsc`, and `codegen.qsc` are compiler stages implemented in Quill itself. `bootstrap.qsc` is their command-line driver. Build and run the stage-0 compiler with:

```bash
bash bootstrap.sh examples/bootstrap_program.qsc /tmp/quill-bootstrap-program
/tmp/quill-bootstrap-program
```

The current stage-0 code generator accepts integer declarations and assignments, numeric functions with parameters and calls, returns, integer literals and identifiers, unary minus, `+`, `-`, `*`, `/`, `%`, comparisons, `if`/`else`, `while`, and `say`/`printf` output. It writes LLVM IR and relies on Clang for native code generation.

The production compiler still builds stage 0. Stage 0 does not yet compile its own implementation: multi-parameter functions, strings, collections, general type support, and complete diagnostics remain before a self-hosting bootstrap chain is possible.
