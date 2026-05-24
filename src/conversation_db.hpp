// Conversation database - SQLite persistence for chat history.
// Stores conversations and messages for /resume functionality.

#ifndef CONVERSATION_DB_HPP
#define CONVERSATION_DB_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace conversation_db {

struct ConversationInfo {
    int64_t id;
    std::string title;
    std::string created_at;
    std::string model_path;
    std::string backend;
};

struct MessageInfo {
    std::string role;
    std::string content;
};

class ConversationDB {
public:
    // Open (or create) the database at the given path.
    explicit ConversationDB(std::string_view db_path);
    ~ConversationDB();

    // Non-copyable, non-movable (owns sqlite3* handle).
    ConversationDB(const ConversationDB&) = delete;
    ConversationDB& operator=(const ConversationDB&) = delete;
    ConversationDB(ConversationDB&&) = delete;
    ConversationDB& operator=(ConversationDB&&) = delete;

    // Returns true if the database was opened successfully.
    bool is_open() const { return db_ != nullptr; }

    // Create tables if they don't exist.
    bool init();

    // Create a new conversation. Returns its ID, or -1 on failure.
    int64_t create_conversation(std::string_view model_path,
                                std::string_view backend,
                                std::string_view title);

    // Save a message for a conversation.
    bool save_message(int64_t conversation_id,
                      std::string_view role,
                      std::string_view content);

    // Update the title of a conversation.
    bool update_title(int64_t conversation_id, std::string_view title);

    // List all conversations, newest first.
    std::vector<ConversationInfo> list_conversations();

    // Get all messages for a conversation, ordered by creation time.
    std::vector<MessageInfo> get_messages(int64_t conversation_id);

    // Get a single conversation by ID.
    ConversationInfo get_conversation(int64_t conversation_id);

    // Get the default database path (~/.cache/tiny-habibi/conversations.db).
    static std::string default_path();

private:
    void* db_ = nullptr;
};

} // namespace conversation_db

#endif // CONVERSATION_DB_HPP
