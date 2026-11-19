"""5.00.05: accounts.db kept on GitHub (works on MP Railway, Railway, a VPS or the GitHub cloud runner).

The database is saved AES-256-GCM encrypted (key from AUTH_PRIVATE_KEY, same format the cloud runner always
used: base64(nonce || ciphertext), AAD "accounts.db") to `accounts.db.enc` on the `mp-railway-data` branch,
through the GitHub REST API (no git needed in the container). Every save is a normal commit, so the branch
history is a full backup: any older copy can be brought back.

Safety (the "everything was reset" bug): a save NEVER replaces the GitHub copy with a database that has fewer
accounts than the one restored at start (the bot never deletes accounts), and nothing is saved at all when the
GitHub copy exists but could not be read (network down, wrong key): the bot then runs, but the good copy on
GitHub stays untouched until the next start.

Settings (environment):
  DATA_REPO        owner/repo for the data branch (default: GITHUB_REPOSITORY, else ThirtyFoxMCx00E1/discordpy-bot)
  DATA_REPO_TOKEN  a GitHub token with contents:write on DATA_REPO (GITHUB_TOKEN works in GitHub Actions)
  DATA_BRANCH      default mp-railway-data
"""
import base64
import hashlib
import json
import logging
import os
import sqlite3
import tempfile
import time
import urllib.error
import urllib.request

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

log = logging.getLogger("mpbot.ghstore")
API = "https://api.github.com"
FILE = "accounts.db.enc"


def db_key(private_hex):
    return hashlib.sha256(b"WAGU-DB1|" + (private_hex or "").strip().lower().encode()).digest()


def count_accounts(path):
    try:
        c = sqlite3.connect(path, timeout=10)
        try:
            n = c.execute("SELECT COUNT(*) FROM accounts").fetchone()[0]
            try:   # 5.00.05: + accounts removed with Discord /delete (on purpose, not lost)
                n += c.execute("SELECT COUNT(*) FROM deleted").fetchone()[0]
            except sqlite3.Error:
                pass
            return n
        finally:
            c.close()
    except Exception:
        return 0


def snapshot(path):
    """A consistent copy of the database while the bot writes to it -> bytes."""
    fd, tmp = tempfile.mkstemp(suffix=".db")
    os.close(fd)
    try:
        src = sqlite3.connect(path, timeout=10)
        dst = sqlite3.connect(tmp)
        src.backup(dst)
        dst.close()
        src.close()
        with open(tmp, "rb") as f:
            return f.read()
    finally:
        os.remove(tmp)


class GhStore:
    def __init__(self, repo, token, private_hex, branch="mp-railway-data"):
        self.repo, self.token, self.branch = repo, token, branch
        self.key = db_key(private_hex)
        self.sha = None              # blob sha of accounts.db.enc on the branch (needed to update it)
        self.restored = None         # accounts in the copy restored at start (None = nothing restored yet)
        self.blocked = False         # GitHub copy exists but could not be read: never overwrite it
        self.last_hash = None
        self.last_save = 0

    @classmethod
    def from_env(cls, private_hex):
        token = os.environ.get("DATA_REPO_TOKEN", "").strip()
        if not token or not private_hex:
            return None
        repo = os.environ.get("DATA_REPO") or os.environ.get("GITHUB_REPOSITORY") or "ThirtyFoxMCx00E1/discordpy-bot"
        return cls(repo, token, private_hex, os.environ.get("DATA_BRANCH", "mp-railway-data"))

    # ------------------------------------------------------------------ REST
    def _req(self, method, path, body=None, accept="application/vnd.github+json"):
        req = urllib.request.Request(API + path, method=method, data=json.dumps(body).encode() if body is not None else None,
                                     headers={"Authorization": f"Bearer {self.token}", "Accept": accept,
                                              "User-Agent": "mpbot-ghstore", "X-GitHub-Api-Version": "2022-11-28"})
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.status, r.read()

    def _meta(self):
        """(sha, encrypted text) of accounts.db.enc, (None, None) when the branch / file does not exist."""
        try:
            _, raw = self._req("GET", f"/repos/{self.repo}/contents/{FILE}?ref={self.branch}")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None, None
            raise
        j = json.loads(raw)
        content = base64.b64decode(j.get("content") or "") if j.get("content") else None
        if content is None or j.get("encoding") != "base64" or (j.get("size", 0) and len(content) != j["size"]):
            _, content = self._req("GET", f"/repos/{self.repo}/contents/{FILE}?ref={self.branch}", accept="application/vnd.github.raw")
        return j["sha"], content

    # ------------------------------------------------------------------ restore / save
    def decrypt(self, enc_text):
        raw = base64.b64decode(enc_text)
        return AESGCM(self.key).decrypt(raw[:12], raw[12:], b"accounts.db")

    def restore(self, db_path, tries=5):
        """Called before the bot opens the database. Returns 'restored' / 'empty' / 'failed'."""
        os.makedirs(os.path.dirname(os.path.abspath(db_path)), exist_ok=True)
        for i in range(tries):
            try:
                sha, enc = self._meta()
                if sha is None:
                    log.info("GitHub data: no saved database on %s@%s yet (first start)", self.repo, self.branch)
                    self.restored = count_accounts(db_path) if os.path.exists(db_path) else 0
                    return "empty"
                plain = self.decrypt(enc)
                local = count_accounts(db_path) if os.path.exists(db_path) else 0
                tmp = db_path + ".gh"
                with open(tmp, "wb") as f:
                    f.write(plain)
                remote = count_accounts(tmp)
                if local > remote:
                    # the disk copy (Railway volume / VPS) is newer than GitHub: keep it, it is saved next
                    os.remove(tmp)
                    log.info("GitHub data: local database has %d accounts, GitHub %d - keeping the local one", local, remote)
                    self.restored = local
                else:
                    os.replace(tmp, db_path)
                    self.restored = remote
                    log.info("GitHub data: restored accounts.db (%d accounts, %d bytes)", remote, len(plain))
                self.sha = sha
                return "restored"
            except Exception as e:
                log.warning("GitHub data: restore try %d failed: %s", i + 1, e)
                time.sleep(min(20, 2 + i * 4))
        self.blocked = True
        log.error("GitHub data: could not read the saved database - NOT saving this run, so the GitHub copy stays safe")
        return "failed"

    def save(self, db_path, reason="periodic"):
        if self.blocked or not os.path.exists(db_path):
            return False
        plain = snapshot(db_path)
        h = hashlib.sha256(plain).hexdigest()
        if h == self.last_hash:
            return False
        fd, tmp = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        try:
            with open(tmp, "wb") as f:
                f.write(plain)
            n = count_accounts(tmp)
        finally:
            os.remove(tmp)
        if self.restored is not None and n < self.restored:
            log.error("GitHub data: refusing to save %d accounts over %d (would lose accounts) - check the database", n, self.restored)
            return False
        nonce = os.urandom(12)
        enc = base64.b64encode(nonce + AESGCM(self.key).encrypt(nonce, plain, b"accounts.db"))
        body = {"message": f"accounts.db: {n} accounts ({reason})", "branch": self.branch,
                "content": base64.b64encode(enc).decode(),
                "committer": {"name": "mp-railway", "email": "mp-railway@users.noreply.github.com"}}
        for attempt in range(2):
            if self.sha:
                body["sha"] = self.sha
            try:
                _, raw = self._req("PUT", f"/repos/{self.repo}/contents/{FILE}", body)
                self.sha = json.loads(raw)["content"]["sha"]
                self.last_hash, self.last_save = h, time.time()
                self.restored = max(self.restored or 0, n)
                log.info("GitHub data: saved accounts.db (%d accounts, %s)", n, reason)
                return True
            except urllib.error.HTTPError as e:
                if e.code in (409, 422) and attempt == 0:   # sha moved (other writer) or branch missing
                    try:
                        self.sha, _ = self._meta()
                    except Exception:
                        pass
                    if self.sha is None and e.code == 422:
                        self._make_branch()
                    continue
                log.warning("GitHub data: save failed (%s): %s", e.code, e.read()[:200])
                return False
            except Exception as e:
                log.warning("GitHub data: save failed: %s", e)
                return False
        return False

    def _make_branch(self):
        try:
            _, raw = self._req("GET", f"/repos/{self.repo}")
            base = json.loads(raw)["default_branch"]
            _, raw = self._req("GET", f"/repos/{self.repo}/git/ref/heads/{base}")
            sha = json.loads(raw)["object"]["sha"]
            self._req("POST", f"/repos/{self.repo}/git/refs", {"ref": f"refs/heads/{self.branch}", "sha": sha})
            log.info("GitHub data: made the %s branch", self.branch)
        except Exception as e:
            log.warning("GitHub data: could not make the branch: %s", e)
