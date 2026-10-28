"""Forgot the password on a PC install? Run with the backend stopped:

    python reset_password.py            # asks for the new password
    python reset_password.py --email you@example.com

It sets the account's password (signing out every session) and clears login lockouts.
In the Android app use "Esqueci a senha" on the login screen instead.
"""
import argparse
import asyncio
import getpass
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(Path(__file__).parent / ".env")

from auth import MIN_PASSWORD, hash_password  # noqa: E402
from core import db  # noqa: E402


async def main():
    ap = argparse.ArgumentParser(description="Redefine a senha de um usuário do LUMNA")
    ap.add_argument("--email", help="padrão: o único usuário / ADMIN_EMAIL")
    args = ap.parse_args()
    users = await db.users.find().to_list(50)
    user = next((u for u in users if u["email"] == (args.email or "").strip().lower()), None) if args.email else (users[0] if len(users) == 1 else None)
    if not user:
        raise SystemExit("Usuário não encontrado. Use --email (cadastrados: " + ", ".join(u["email"] for u in users) + ")")
    pw = getpass.getpass(f"Nova senha para {user['email']}: ")
    if len(pw) < MIN_PASSWORD or pw != getpass.getpass("Repita: "):
        raise SystemExit(f"Senhas diferentes ou com menos de {MIN_PASSWORD} caracteres")
    await db.users.update_one({"_id": user["_id"]}, {"$set": {"password_hash": hash_password(pw), "password_changed": True,
                                                              "pw_version": user.get("pw_version", 0) + 1}})
    await db.login_attempts.delete_many({})
    print("Senha redefinida. Todas as sessões foram encerradas.")


asyncio.run(main())
