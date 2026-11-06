namespace pt {
void LogWrite(LogLevel, std::string_view text) { std::printf("%.*s\n", static_cast<int>(text.size()), text.data()); }
}
