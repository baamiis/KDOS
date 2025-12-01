# Contributing to KDOS

Thank you for your interest in contributing to KDOS (formerly KTOS)! This document provides guidelines and instructions for contributing to our tiny cooperative RTOS.

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Coding Standards](#coding-standards)
- [Commit Guidelines](#commit-guidelines)
- [Pull Request Process](#pull-request-process)
- [Testing](#testing)
- [Documentation](#documentation)

## Code of Conduct

This project adheres to a Code of Conduct. By participating, you are expected to uphold this code. Please report unacceptable behavior to [your-email]@[domain].

## Getting Started

### Prerequisites
- Git
- Appropriate toolchain for your target platform (e.g., gcc-arm-none-eabi for ARM)
- Basic understanding of embedded systems and C programming

### Setting Up Your Development Environment

1. **Fork the repository**
   - Click the "Fork" button on GitHub
   - Clone your fork locally:
     ```bash
     git clone https://github.com/YOUR-USERNAME/ktos.git
     cd ktos
     ```

2. **Add upstream remote**
   ```bash
   git remote add upstream https://github.com/ORIGINAL-OWNER/ktos.git
   ```

3. **Build the project**
   ```bash
   make all
   ```

4. **Run tests**
   ```bash
   make test
   ```

## Development Workflow

1. **Create a branch** for your work
   ```bash
   git checkout -b feature/your-feature-name
   ```
   
   Branch naming conventions:
   - `feature/description` - for new features
   - `fix/description` - for bug fixes
   - `docs/description` - for documentation
   - `refactor/description` - for code refactoring
   - `test/description` - for test improvements

2. **Make your changes**
   - Write clean, well-documented code
   - Follow the coding standards below
   - Add tests for new functionality

3. **Keep your branch updated**
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

4. **Test your changes**
   - Build the project
   - Run all tests
   - Test on actual hardware if possible

5. **Commit your changes**
   - Follow the commit message guidelines below
   - Keep commits focused and atomic

6. **Push to your fork**
   ```bash
   git push origin feature/your-feature-name
   ```

7. **Open a Pull Request**
   - Go to the original repository on GitHub
   - Click "New Pull Request"
   - Select your branch
   - Fill out the PR template completely

## Coding Standards

### C Code Style

- **Indentation**: 4 spaces (no tabs)
- **Line length**: Maximum 100 characters
- **Braces**: K&R style
  ```c
  if (condition) {
      do_something();
  }
  ```

- **Naming conventions**:
  - Functions: `k_function_name` or `KFunction` (follow existing style in KDOS)
  - Task functions: descriptive names like `task_timer`, `task_serial`
  - Types: `snake_case_t` (e.g., `task_handle_t`)
  - Macros/Constants: `UPPER_CASE` (e.g., `KDOS_MAX_TASKS`, `MSG_TYPE_INIT`)
  - Keep consistency with existing KDOS code style

- **Comments**:
  - Use `/* */` for multi-line comments
  - Use `//` for single-line comments
  - Document all public APIs with descriptive comments
  - Explain "why" not "what" in comments

### Code Organization

- Keep functions small and focused (ideally < 50 lines)
- One function should do one thing
- Minimize global state
- Use static functions for internal implementation details
- Group related functions together

### Memory Management

- **No dynamic allocation** in core KTOS code
- All memory should be statically allocated or stack-based
- Document memory requirements clearly
- Avoid recursion to prevent stack overflow

### Platform-Specific Code

- Keep platform-specific code isolated in separate files
- Use clear abstractions for hardware access
- Document platform requirements

## Commit Guidelines

### Commit Message Format

```
<type>: <subject>

<body>

<footer>
```

**Type** must be one of:
- `feat`: A new feature
- `fix`: A bug fix
- `docs`: Documentation only changes
- `style`: Code style changes (formatting, missing semicolons, etc.)
- `refactor`: Code change that neither fixes a bug nor adds a feature
- `perf`: Performance improvement
- `test`: Adding or updating tests
- `chore`: Changes to build process or auxiliary tools

**Subject**:
- Use imperative mood ("add feature" not "added feature")
- Don't capitalize first letter
- No period at the end
- Maximum 50 characters

**Body** (optional):
- Explain what and why, not how
- Wrap at 72 characters

**Footer** (optional):
- Reference issues: `Closes #123` or `Fixes #456`

### Examples

```
feat: add semaphore support for task synchronization

Implements counting semaphores to allow tasks to synchronize
access to shared resources. Includes wait and signal operations
with timeout support.

Closes #42
```

```
fix: prevent race condition in scheduler

The task ready queue was not properly protected during interrupt
context, leading to occasional task corruption.
```

## Pull Request Process

1. **Before submitting**:
   - Ensure all tests pass
   - Update documentation if needed
   - Rebase on latest main branch
   - Ensure your branch has a clear, linear history

2. **PR Description**:
   - Fill out the entire PR template
   - Explain what changed and why
   - Link related issues
   - Include test results

3. **Review Process**:
   - At least one maintainer must approve
   - All CI checks must pass
   - All review comments must be addressed
   - Conversation must be resolved

4. **After approval**:
   - Maintainers will merge your PR
   - You can delete your branch after merging

## Testing

### Unit Tests

- Write tests for all new functionality
- Ensure existing tests still pass
- Aim for high code coverage
- Test edge cases and error conditions

### Hardware Testing

When possible, test on actual hardware:
- Document which platform(s) you tested on
- Include test results in your PR
- Note any platform-specific behaviors

### Test Naming

```c
void test_task_create_success(void);
void test_task_create_null_pointer(void);
void test_scheduler_round_robin(void);
```

## Documentation

### Code Documentation

- Document all public APIs
- Use clear, descriptive comments
- Include parameter descriptions
- Document return values and error conditions
- Provide usage examples for complex functions

Example:
```c
/**
 * Create a new task in the KTOS scheduler
 * 
 * @param task_func Pointer to the task function
 * @param priority Task priority (0 = highest)
 * @param stack_size Size of task stack in bytes
 * @return Task handle on success, NULL on failure
 * 
 * Example:
 *   task_handle_t task = ktos_task_create(my_task, 0, 512);
 *   if (task == NULL) {
 *       // Handle error
 *   }
 */
task_handle_t ktos_task_create(task_func_t task_func, 
                                uint8_t priority, 
                                size_t stack_size);
```

### README and Guides

- Update README.md if you add new features
- Add examples for new functionality
- Update architecture docs if changing core behavior

## Questions?

- Open an issue with the `question` label
- Join our discussions (if you have a discussion forum)
- Contact maintainers

## Recognition

Contributors will be recognized in:
- CONTRIBUTORS.md file
- Release notes
- Project documentation

Thank you for contributing to KTOS!
