// ─── stb_image (single-header image loader) ─────────────────────────────────
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ─── Standard Library ────────────────────────────────────────────────────────
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
namespace fs = filesystem;

// ─── Constants ───────────────────────────────────────────────────────────────
static const int  NUM_COLORS   = 5;
static const int  QUANT_BITS   = 4;   // reduce each channel to 4-bit → 16 levels
static const int  QUANT_STEP   = 1 << (8 - QUANT_BITS); // = 16

// ─── Data Types ──────────────────────────────────────────────────────────────
struct RGB {
    uint8_t r, g, b;
};

struct ColorEntry {
    RGB    color;
    size_t count;
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Convert an RGB triple to a "#RRGGBB" hex string (uppercase)
string rgb_to_hex(const RGB& c) {
    ostringstream oss;
    oss << "#"
        << uppercase << hex << setfill('0')
        << setw(2) << (int)c.r
        << setw(2) << (int)c.g
        << setw(2) << (int)c.b;
    return oss.str();
}

// Quantise a single channel value to reduce the colour space
uint8_t quantise(uint8_t v) {
    return static_cast<uint8_t>((v / QUANT_STEP) * QUANT_STEP);
}

// Return a simple 32-bit key for a quantised RGB triple
uint32_t color_key(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) <<  8) |
            static_cast<uint32_t>(b);
}

// Current timestamp as "YYYY-MM-DD HH:MM:SS"
string current_timestamp() {
    auto now   = chrono::system_clock::now();
    time_t t = chrono::system_clock::to_time_t(now);
    tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

// Strip leading/trailing whitespace
string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end   = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ANSI colour block
string ansi_swatch(const RGB& c) {
    // Background colour escape + 3 spaces + reset
    return "\033[48;2;" +
           to_string(c.r) + ";" +
           to_string(c.g) + ";" +
           to_string(c.b) + "m   \033[0m";
}

// ─── Color Extraction ────────────────────────────────────────────────────────
vector<ColorEntry> extract_colors(const string& path, int n = NUM_COLORS) {
    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 3);
    if (!data) {
        throw runtime_error("Failed to load image: " + path);
    }

    map<uint32_t, size_t> freq;
    map<uint32_t, RGB>    color_map;

    const int total_pixels = width * height;
    for (int i = 0; i < total_pixels; ++i) {
        uint8_t r = quantise(data[i * 3 + 0]);
        uint8_t g = quantise(data[i * 3 + 1]);
        uint8_t b = quantise(data[i * 3 + 2]);

        uint32_t key = color_key(r, g, b);
        freq[key]++;
        color_map[key] = {r, g, b};
    }

    stbi_image_free(data);

    // Sort by frequency descending
    vector<pair<uint32_t, size_t>> sorted_freq(freq.begin(), freq.end());
    sort(sorted_freq.begin(), sorted_freq.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });

    vector<ColorEntry> result;
    int limit = min(n, (int)sorted_freq.size());
    for (int i = 0; i < limit; ++i) {
        uint32_t key = sorted_freq[i].first;
        result.push_back({ color_map[key], sorted_freq[i].second });
    }
    return result;
}

// ─── Display ─────────────────────────────────────────────────────────────────
void display_palette(const vector<ColorEntry>& palette) {
    cout << "\n  Extracted Colors:\n";
    cout << "  " << string(36, '-') << "\n";
    for (int i = 0; i < (int)palette.size(); ++i) {
        string hex = rgb_to_hex(palette[i].color);
        cout << "  [" << (i + 1) << "]  "
                  << ansi_swatch(palette[i].color)
                  << "  " << hex
                  << "   (R:" << (int)palette[i].color.r
                  << " G:" << (int)palette[i].color.g
                  << " B:" << (int)palette[i].color.b << ")\n";
    }
    cout << "  " << string(36, '-') << "\n";
}
// ─── History (in-memory, session only) ───────────────────────────────────────
struct HistoryEntry {
    string              timestamp;
    string              image_name;
    vector<ColorEntry>  palette;
};

static vector<HistoryEntry> history;

void save_history(const string& image_name,
                  const vector<ColorEntry>& palette)
{
    if (palette.empty()) {
        cout << "Nothing to save. Extract colors from an image first.\n";
        return;
    }

    history.push_back({ current_timestamp(),
                        image_name.empty() ? "Unknown" : image_name,
                        palette });

    cout << "\033[32m✔ Saved to history (entry #" << history.size() << ").\033[0m\n";
}

void view_history() {
    if (history.empty()) {
        cout << "No history yet. Save some colors first!\n";
        return;
    }

    string sep(40, '=');
    cout << "\n--- Color History (" << history.size() << " entries) ---\n";
    for (int i = 0; i < (int)history.size(); ++i) {
        const auto& e = history[i];
        cout << "\n" << sep << "\n";
        cout << "Entry     : #" << (i + 1) << "\n";
        cout << "Date/Time : " << e.timestamp  << "\n";
        cout << "Image     : " << e.image_name << "\n";
        cout << "Colors    :\n";
        for (const auto& c : e.palette) {
            cout << "    " << ansi_swatch(c.color)
                      << "  " << rgb_to_hex(c.color)
                      << "  (R:" << (int)c.color.r
                      << " G:"   << (int)c.color.g
                      << " B:"   << (int)c.color.b << ")\n";
        }
    }
    cout << "\n" << sep << "\n--- End of History ---\n";
}

void clear_history() {
    if (history.empty()) {
        cout << "No history to clear.\n";
        return;
    }
    cout << "Are you sure you want to clear all history? (y/N): ";
    string ans;
    getline(cin, ans);
    if (trim(ans) == "y" || trim(ans) == "Y") {
        history.clear();
        cout << "  \033[32m✔ History cleared.\033[0m\n";
    } else {
        cout << "  Cancelled.\n";
    }
}

// ─── Main Menu ───────────────────────────────────────────────────────────────
void print_menu(bool has_palette) {
    cout << "What's That Color?\n";
    cout << "Image Color Palette Extractor\n";
    cout << "=============== Menu ================\n";
    cout << "1. Load image & extract colors\n";
    if (has_palette) {
    cout << "2. Show current palette\n";
    cout << "3. Save palette to history\n";
    } else {
    cout << "2. (load an image first)\n";
    cout << "3. (load an image first)\n";
    }
    cout << "4. View history\n";
    cout << "5. Clear history\n";
    cout << "6. Quit\n";
    cout << "=====================================";
    cout << "\nChoice: ";
}

int main(int argc, char* argv[]) {
    vector<ColorEntry> palette;
    string             image_name;

    // If an image path was passed on the command line, load it immediately
    if (argc >= 2) {
        string path = argv[1];
        try {
            palette    = extract_colors(path);
            image_name = fs::path(path).filename().string();
            cout << "\nLoaded: " << image_name << "\n";
            display_palette(palette);
        } catch (const exception& e) {
            cerr << "Error: " << e.what() << "\n";
        }
    }

    // Interactive menu loop
    while (true) {
        print_menu(!palette.empty());
        string line;
        getline(cin, line);
        int choice = 0;
        try { choice = stoi(trim(line)); } catch (...) {}

        switch (choice) {
            case 1: {
                cout << "Image path: ";
                string path;
                getline(cin, path);
                path = trim(path);
                // Remove surrounding quotes if present
                if (!path.empty() && path.front() == '"') path = path.substr(1);
                if (!path.empty() && path.back()  == '"') path.pop_back();
                try {
                    palette    = extract_colors(path);
                    image_name = fs::path(path).filename().string();
                    cout << "Loaded: " << image_name << "\n";
                    display_palette(palette);
                } catch (const exception& e) {
                    cerr << "Error: " << e.what() << "\n";
                }
                break;
            }
            case 2:
                if (!palette.empty()) display_palette(palette);
                else cout << "No palette loaded yet.\n";
                break;

            case 3:
                save_history(image_name, palette);
                break;

            case 4:
                view_history();
                break;

            case 5:
                clear_history();
                break;

            case 6:
                cout << "\nBye!\n";
                return 0;

            default:
                cout << "Invalid choice. Try 1-6.\n";
        }
    }
}