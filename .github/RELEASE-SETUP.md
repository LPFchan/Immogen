# Release workflow setup

The **Release** workflow builds firmware and creates a GitHub release when you push a `v*` tag. It needs a Personal Access Token with permission to create releases.

## One-time setup

1. **Create a Personal Access Token (classic)**  
   - GitHub → your profile menu → **Settings** → **Developer settings** → **Personal access tokens** → **Tokens (classic)**  
   - **Generate new token (classic)**  
   - Name it e.g. `Release workflow`.  
   - Enable scope: **repo** (full control of private repositories).  
   - Generate and **copy the token** (you won’t see it again).

2. **Add the token as a repo secret**  
   - In **this** repo → **Settings** → **Secrets and variables** → **Actions**  
   - **New repository secret**  
   - Name: `RELEASE_TOKEN`  
   - Value: paste the token  
   - Save.

After that, pushing a tag (e.g. `git push origin v0.1.0`) will run the workflow and create a release with `firmware.hex` and `firmware.zip`.
