%module _iter_cov_json

%include "typemaps.i"
%include "std_string.i"
%include "std_vector.i"

%inline %{
typedef std::vector<std::string> VecStr;

struct TState {
    uint64_t data_size;
    uint32_t bracers;
    uint64_t start_pos;
    uint64_t global_pos;

    bool in_string;
    std::string key_name;
    std::string buffer;

    std::vector<VecStr> target_blocks;
    VecStr keys_stack;
    std::vector<uint32_t> keys_stack_bracers;

    VecStr curr_block;
    uint32_t curr_block_bracers;
} State;

void init_state() {
    State.data_size = 0;
    State.bracers = 0;
    State.start_pos = 0;
    State.global_pos = 0;

    State.in_string = false;
    State.key_name = "";
    State.buffer = "";

    State.target_blocks = {{"data", "files"}, {"data", "functions"}};
    State.keys_stack = {};
    State.keys_stack_bracers = {};

    State.curr_block = {};
    State.curr_block_bracers = 0;
}

PyObject* get_tail() {
    if (State.bracers) {
        PyErr_Format(PyExc_RuntimeError, "Bracers are not balanced: %d", State.bracers);
        return NULL;
    }
    std::string tail = State.buffer;

    // drop buffers
    State.key_name = "";
    State.buffer = "";
    State.keys_stack = {};
    State.keys_stack_bracers = {};
    State.curr_block = {};

    return Py_BuildValue("s#", tail.c_str(), tail.size());
}

PyObject* transform(std::vector< std::pair<VecStr, std::string> >& blocks) {
    // generate list of two-elem-tuples with block_path and block_data
    PyObject* result = PyList_New(0);
    for (auto& entry: blocks) {
        // Py_BuildValue("s#", ) truncates huge strings - use "s" instead
        // see test_huge_json() test case
        // in the devtools/ya/test/programs/test_tool/resolve_clang_coverage/iter_cov_json/tests/fat/test.py
        PyObject* block_data = Py_BuildValue("s", entry.second.c_str());

        PyObject* block_path = Py_None;
        if (!entry.first.empty()) {
            block_path = PyList_New(0);
            for (auto& part: entry.first) {
                PyObject* block_name = Py_BuildValue("s#", part.c_str(), part.size());
                PyList_Append(block_path, block_name);
                Py_DECREF(block_name);
            }
        }

        PyObject* res_entry = Py_BuildValue("OO", block_path, block_data);
        if (block_path != Py_None)
            Py_DECREF(block_path);
        Py_DECREF(block_data);

        PyList_Append(result, res_entry);
        Py_DECREF(res_entry);
    }
    return result;
}

PyObject* iter_cov(PyObject *str) {
    std::string data = PyString_AsString(str);
    std::vector< std::pair<VecStr, std::string> > blocks;

    State.data_size = data.size();
    State.start_pos = 0;

    for (uint64_t curr_pos = 0; curr_pos < State.data_size; ++curr_pos) {
        const char symbol = data[curr_pos];

        // we are in the target_block (most likely) - just count bracers to speedup processing
        if (State.curr_block_bracers and State.curr_block_bracers < State.bracers) {
            if (symbol == '{' || symbol == '[')
                ++State.bracers;
            else if (symbol == '}' || symbol == ']')
                --State.bracers;
            continue;
        }
        else if ((symbol == ',' || symbol == ']') && State.curr_block_bracers == State.bracers) {
            uint64_t offset = curr_pos - State.start_pos;
            blocks.push_back({State.curr_block, State.buffer + data.substr(State.start_pos, offset)});
            blocks.push_back({{}, std::string(1, symbol)});

            State.buffer = "";
            State.start_pos = curr_pos + 1; // +1 because current symbol is yielded
        }

        if (symbol == '{') {
            ++State.bracers;
        }
        else if (symbol == '}') {
            if (!State.bracers) {
                PyErr_Format(PyExc_RuntimeError, "Unexpected closing bracer at %llu pos", (unsigned long long)State.global_pos);
                return NULL;
            }
            --State.bracers;

            if (!State.keys_stack.empty() && State.keys_stack_bracers.back() == State.bracers) {
                State.keys_stack.pop_back();
                State.keys_stack_bracers.pop_back();
            }
        }
        else if (symbol == '[') {
            ++State.bracers;

            // if keys_stack in target_blocks
            if (std::find(State.target_blocks.cbegin(), State.target_blocks.cend(), State.keys_stack) != State.target_blocks.cend()) {
                // header
                uint64_t offset = curr_pos - State.start_pos + 1;
                blocks.push_back({{}, State.buffer + data.substr(State.start_pos, offset)});
                State.buffer = "";
                State.start_pos += offset;

                State.curr_block = State.keys_stack;
                State.curr_block_bracers = State.bracers;
            }
        }
        else if (symbol == ']') {
            if (State.curr_block_bracers == State.bracers)
                State.curr_block_bracers = 0;

            if (!State.bracers) {
                PyErr_Format(PyExc_RuntimeError, "Unexpected closing bracer at %llu pos", (unsigned long long)State.global_pos);
                return NULL;
            }
            --State.bracers;
        }
        else if (symbol == '"') {
            State.in_string ^= true;
            if (State.in_string)
                State.key_name = "";
        }
        else if (State.in_string) {
            State.key_name += symbol;
        }
        else if (symbol == ':') {
            if (!State.keys_stack.empty() && State.keys_stack_bracers.back() == State.bracers) {
                // same level - replace current dict key
                State.keys_stack.back() = State.key_name;
            }
            else {
                // going deeper
                State.keys_stack.push_back(State.key_name);
                State.keys_stack_bracers.push_back(State.bracers);
            }
        }
    }
    State.buffer += data.substr(State.start_pos);

    return transform(blocks);
}
%}
