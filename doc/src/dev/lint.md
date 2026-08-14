# Linting

## black

We format Python code with [black].

```sh
uv run --dev black src
```

[black]: https://github.com/psf/black

## isort

We format Python code with [isort].

```sh
uv run --dev isort src
```

[isort]: https://isort.readthedocs.io/en/latest/

## ruff

We lint Python code with [ruff].

```sh
uv run --dev ruff check src
```

ruff has a "fix" mode:

```sh
uv run --dev ruff check --fix src
```

[ruff]: https://docs.astral.sh/ruff/

## mypy

We type-check Python code with [mypy].

```bash
uv run --dev mypy src
```

[mypy]: https://mypy-lang.org/

## typos

We spell-check Markdown with [typos]:

```sh
find . -type f -name '*.md' -print | typos --file-list -
```

[typos]: https://github.com/crate-ci/typos

