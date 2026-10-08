# Efficient Computing for AI — Quarto Course Site Template

A Markdown-first static website template for university course material, using **Quarto**, **GitHub Actions**, and **GitHub Pages**.

## 1. Create the GitHub repository

Create a **public** GitHub repository, for example:

```text
efficient-computing-ai
```

Copy the contents of this template into that repository.

## 2. Change the GitHub repository link

Edit `_quarto.yml` and replace:

```yaml
href: https://github.com/YOUR_USERNAME/YOUR_REPOSITORY
```

with the actual repository URL.

## 3. Install Quarto locally

Install Quarto from:

<https://quarto.org/docs/get-started/>

Then verify:

```bash
quarto --version
```

## 4. Preview locally

From the repository root:

```bash
quarto preview
```

Quarto will start a local development server and rebuild pages as you edit them.

## 5. Render once before the first push

```bash
quarto render
```

Generated output is ignored by Git.

## 6. Push to GitHub

```bash
git init
git branch -M main
git add .
git commit -m "docs: initialize course website"
git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
git push -u origin main
```

## 7. Enable GitHub Pages

The included `publish.yml` workflow publishes the rendered site to a `gh-pages` branch using Quarto's GitHub Pages publishing action.

After the first workflow run, check:

In the GitHub repository, open **Settings → Pages**.

and make sure Pages is configured to use the `gh-pages` branch when GitHub has not enabled it automatically.

The default project-site URL is:

```text
https://YOUR_USERNAME.github.io/YOUR_REPOSITORY/
```

## Recommended authoring convention

Use `.md` for normal content:

```text
lectures/03-quantization.md
labs/lab03/setup.md
labs/lab03/assignment.md
```

Use `.qmd` when a page genuinely needs executable code or another Quarto feature that benefits from computation.

## Validation

The repository contains two workflows:

- `validate.yml`: renders the site, lints Markdown, and checks links.
- `publish.yml`: renders and publishes the site to GitHub Pages after changes reach `main`.

The site does **not** require a server, database, React application, or custom backend.
