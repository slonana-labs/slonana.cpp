# Contributing to Slonana C++ Validator

Welcome to the Slonana C++ validator project! This guide outlines our disciplined development workflow designed to maintain code quality and prevent CI failures through local validation.

## 🎯 Disciplined Development Workflow

Our development process enforces quality through automated local validation before code reaches CI. This reduces resource waste and maintains high standards.

### Required Before Every Push

**Mandatory**: All developers must run `make ci-fast` before pushing code.

```bash
make ci-fast
```

This command runs:
- ✅ Build validation (cmake + make)
- ✅ Test suite execution (ctest)
- ✅ Code formatting check (clang-format)

**Automatic Enforcement**: Install the pre-push hook to enforce this automatically:

```bash
make setup-hooks
```

The hook will block pushes that fail basic validation, preventing CI failures from formatting violations, build errors, or test failures.

### Required Before Every PR

**Mandatory**: All developers must run `make bench-local` before opening pull requests.

```bash
make bench-local
```

This command runs:
- ✅ Full `ci-fast` validation
- ✅ Local Slonana benchmark (30s duration)
- ✅ Local Agave benchmark (30s duration)
- ✅ Performance budget validation against p95 thresholds

**Performance Budgets**: PRs are automatically blocked if they regress performance beyond these thresholds:
- **RPC p95 latency**: 15ms maximum
- **Transaction p95 latency**: 50ms maximum

### One-Time Setup

Install the complete validation workflow:

```bash
# Install dependencies and git hooks
make setup-hooks

# Verify all dependencies are available
make check-deps
```

## 🛠️ Development Commands

### Essential Commands

| Command | Description | When to Use |
|---------|-------------|-------------|
| `make ci-fast` | Build + test + format check | Before every push (enforced by hook) |
| `make bench-local` | Local benchmark comparison + performance validation | Before every PR |
| `make setup-hooks` | Install automated pre-push validation | One-time setup |
| `make check-deps` | Verify all dependencies installed | Environment setup |

### Additional Commands

| Command | Description |
|---------|-------------|
| `make build` | Build validator binary only |
| `make test` | Run test suite only |
| `make format` | Fix code formatting violations |
| `make format-check` | Check formatting without fixing |
| `make lint` | Run static analysis (cppcheck) |
| `make clean` | Clean build artifacts |
| `make show-budgets` | Display current performance budgets |

## 📊 Performance Standards

### Performance Budgets

We maintain strict performance budgets to prevent regressions:

- **RPC p95 Latency**: ≤ 15ms
- **Transaction p95 Latency**: ≤ 50ms

These thresholds are validated in `make bench-local` and will fail if exceeded.

### Benchmark Requirements

Local benchmarks must pass before opening PRs:

1. **Slonana Benchmark**: 30-second test duration with transaction processing
2. **Agave Comparison**: Parallel Agave validator benchmark for performance comparison
3. **Performance Validation**: Automated p95 threshold checking

## 🔧 Environment Setup

### Required Dependencies

- **CMake**: Build system
- **G++**: C++ compiler
- **Solana CLI**: Validator interaction
- **Rust**: Some toolchain components
- **clang-format**: Code formatting
- **cppcheck**: Static analysis (optional)

### Automated Installation

```bash
# Ubuntu/Debian
make install-deps

# Manual verification
make check-deps
```

## 🚀 Workflow Examples

### Standard Development Cycle

```bash
# 1. One-time setup (first contribution)
make setup-hooks

# 2. Make your changes
# ... edit code ...

# 3. Validate locally (before push - enforced by hook)
make ci-fast

# 4. Push changes
git push

# 5. Run performance tests (before PR)
make bench-local

# 6. Open pull request
```

### Fixing Validation Failures

```bash
# Build failures
make build

# Test failures
make test

# Formatting violations
make format

# Performance regressions
make bench-local
# Review benchmark results and optimize code
```

## 🎯 Quality Standards

### Code Quality

- **Zero formatting violations**: Use `make format` to fix
- **All tests passing**: Use `make test` to verify
- **Clean builds**: Use `make build` to check
- **Static analysis clean**: Use `make lint` for checking

### Performance Standards

- **No p95 regressions**: Validated by `make bench-local`
- **Benchmark parity**: Local results must align with CI expectations
- **Resource efficiency**: Tests must complete within reasonable time limits

## 🔒 Enforcement Strategy

### Automated Prevention

1. **Pre-push hook**: Blocks pushes failing `make ci-fast`
2. **Performance budgets**: Block PRs exceeding p95 thresholds
3. **CI validation**: Additional validation in GitHub Actions

### Manual Requirements

1. **Code review**: All PRs require review
2. **Benchmark validation**: Results must be included in PR description
3. **Documentation updates**: Update relevant docs when changing behavior

## 🆘 Troubleshooting

### Common Issues

| Issue | Solution |
|-------|----------|
| `make ci-fast` fails | Run individual targets: `make build`, `make test`, `make format` |
| Benchmark failures | Check validator configuration and system resources |
| Performance budget exceeded | Profile code and optimize hot paths |
| Missing dependencies | Run `make check-deps` and `make install-deps` |

### Getting Help

- **Build issues**: Check CMake configuration and dependencies
- **Test failures**: Review test output and logs
- **Performance issues**: Use profiling tools and benchmark analysis
- **Workflow questions**: Review this guide and Makefile help

## 📝 Best Practices

### Development Habits

- **Frequent validation**: Run `make ci-fast` early and often
- **Performance awareness**: Monitor benchmark results during development
- **Clean commits**: Use descriptive commit messages and atomic changes
- **Documentation**: Update relevant documentation with functional changes

### Performance Optimization

- **Profile first**: Use profiling tools to identify bottlenecks
- **Benchmark locally**: Always validate performance changes with `make bench-local`
- **Consider alternatives**: Evaluate multiple implementation approaches
- **Measure impact**: Document performance improvements in PR descriptions

## 🎯 Success Metrics

A well-disciplined contribution should:

- ✅ Pass `make ci-fast` consistently
- ✅ Meet performance budgets in `make bench-local`
- ✅ Include comprehensive test coverage
- ✅ Maintain or improve documentation quality
- ✅ Demonstrate measurable value (features, performance, stability)

This disciplined approach ensures high code quality, prevents CI resource waste, and maintains the performance standards expected from a production-ready validator implementation.