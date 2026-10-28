"""One-off migration: copy every collection from MongoDB into the SQLite document store.

Usage (backend/.env or environment provides the values):
    MONGO_URL=mongodb://localhost:27017 DB_NAME=test_database python migrate_mongo_to_sqlite.py [--force]

Target: SQLITE_PATH (default backend/data/harness.db). Refuses to write into non-empty
collections unless --force, which clears them first. Secrets stay encrypted as they are
(same SECRETS_KEY required afterwards).
"""
import os
import sys
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(Path(__file__).parent / ".env")

import pymongo  # noqa: E402

from docstore import DocStore  # noqa: E402


def main(force: bool) -> int:
    source = pymongo.MongoClient(os.environ["MONGO_URL"])[os.environ["DB_NAME"]]
    target_path = os.environ.get("SQLITE_PATH") or str(Path(__file__).parent / "data" / "harness.db")
    target = DocStore(target_path)
    names = sorted(n for n in source.list_collection_names() if not n.startswith("system."))
    busy = [n for n in names if target[n].count_documents({})]
    if busy and not force:
        print(f"Coleções já preenchidas em {target_path}: {', '.join(busy)}. Use --force para sobrescrever.")
        return 1
    for name in names:
        target[name].delete_many({})
        docs = list(source[name].find())
        target[name].insert_many(docs)
        print(f"{name}: {len(docs)} documentos")
    print(f"Migração concluída → {target_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main("--force" in sys.argv[1:]))
