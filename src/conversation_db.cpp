// Conversation database implementation using SQLite3 C API.

#include "conversation_db.hpp"
#include <cstdlib>
#include <cstring>
#include <sqlite3.h>
#include <stdexcept>

namespace conversation_db {

static inline sqlite3* to_sqlite3(void* p) { return static_cast<sqlite3*>(p); }

ConversationDB::ConversationDB(std::string_view db_path) {
    sqlite3* raw = nullptr;
    int rc = sqlite3_open(std::string(db_path).c_str(), &raw);
    db_ = (rc == SQLITE_OK) ? raw : nullptr;
}

ConversationDB::~ConversationDB() {
    if (db_) {
        sqlite3_close(to_sqlite3(db_));
        db_ = nullptr;
    }
}

bool ConversationDB::init() {
    if (!db_) return false;
    sqlite3* db = to_sqlite3(db_);

    const char* sql_create_conv = R"(
        CREATE TABLE IF NOT EXISTS conversations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            model_path TEXT NOT NULL,
            backend TEXT NOT NULL,
            title TEXT
        );
    )";

    const char* sql_create_msg = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id INTEGER NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
        );
    )";

    const char* sql_pragma = "PRAGMA foreign_keys = ON;";

    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql_pragma, nullptr, nullptr, nullptr) != SQLITE_OK) return false;
    if (sqlite3_exec(db, sql_create_conv, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        sqlite3_free(err_msg);
        return false;
    }
    if (sqlite3_exec(db, sql_create_msg, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

int64_t ConversationDB::create_conversation(std::string_view model_path,
                                             std::string_view backend,
                                             std::string_view title) {
    if (!db_) return -1;
    sqlite3* db = to_sqlite3(db_);

    const char* sql = R"(
        INSERT INTO conversations (model_path, backend, title)
        VALUES (?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, model_path.data(), static_cast<int>(model_path.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, backend.data(), static_cast<int>(backend.size()), SQLITE_TRANSIENT);
    if (title.empty()) {
        sqlite3_bind_null(stmt, 3);
    } else {
        sqlite3_bind_text(stmt, 3, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
    }

    int64_t id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        id = sqlite3_last_insert_rowid(db);
    }
    sqlite3_finalize(stmt);
    return id;
}

bool ConversationDB::save_message(int64_t conversation_id,
                                   std::string_view role,
                                   std::string_view content) {
    if (!db_) return false;
    sqlite3* db = to_sqlite3(db_);

    const char* sql = R"(
        INSERT INTO messages (conversation_id, role, content)
        VALUES (?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_int64(stmt, 1, conversation_id);
    sqlite3_bind_text(stmt, 2, role.data(), static_cast<int>(role.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.data(), static_cast<int>(content.size()), SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool ConversationDB::update_title(int64_t conversation_id, std::string_view title) {
    if (!db_) return false;
    sqlite3* db = to_sqlite3(db_);

    const char* sql = "UPDATE conversations SET title = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, title.data(), static_cast<int>(title.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, conversation_id);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<ConversationInfo> ConversationDB::list_conversations() {
    std::vector<ConversationInfo> result;
    if (!db_) return result;
    sqlite3* db = to_sqlite3(db_);

    const char* sql = R"(
        SELECT id, COALESCE(title, ''), created_at, model_path, backend
        FROM conversations
        ORDER BY created_at DESC
        LIMIT 50;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ConversationInfo info;
        info.id = sqlite3_column_int64(stmt, 0);
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* backend = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        info.title = title ? title : "";
        info.created_at = created ? created : "";
        info.model_path = model ? model : "";
        info.backend = backend ? backend : "";
        result.push_back(std::move(info));
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<MessageInfo> ConversationDB::get_messages(int64_t conversation_id) {
    std::vector<MessageInfo> result;
    if (!db_) return result;
    sqlite3* db = to_sqlite3(db_);

    const char* sql = R"(
        SELECT role, content FROM messages
        WHERE conversation_id = ?
        ORDER BY id ASC;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;

    sqlite3_bind_int64(stmt, 1, conversation_id);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        MessageInfo msg;
        const char* role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.role = role ? role : "";
        msg.content = content ? content : "";
        result.push_back(std::move(msg));
    }
    sqlite3_finalize(stmt);
    return result;
}

ConversationInfo ConversationDB::get_conversation(int64_t conversation_id) {
    ConversationInfo info;
    info.id = -1;
    if (!db_) return info;
    sqlite3* db = to_sqlite3(db_);

    const char* sql = R"(
        SELECT id, COALESCE(title, ''), created_at, model_path, backend
        FROM conversations WHERE id = ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return info;

    sqlite3_bind_int64(stmt, 1, conversation_id);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        info.id = sqlite3_column_int64(stmt, 0);
        const char* title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* created = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* model = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const char* backend = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        info.title = title ? title : "";
        info.created_at = created ? created : "";
        info.model_path = model ? model : "";
        info.backend = backend ? backend : "";
    }
    sqlite3_finalize(stmt);
    return info;
}

std::string ConversationDB::default_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    if (!home) return "./conversations.db";
    return std::string(home) + "/.cache/tiny-habibi/conversations.db";
}

} // namespace conversation_db
