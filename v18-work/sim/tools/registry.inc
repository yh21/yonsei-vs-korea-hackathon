vector<string> decide_v15A(const p::View&, const p::Init&);
vector<string> decide_v15B(const p::View&, const p::Init&);
vector<string> decide_v16A(const p::View&, const p::Init&);
vector<string> decide_v16B(const p::View&, const p::Init&);
vector<string> decide_v17A(const p::View&, const p::Init&);
vector<string> decide_v17B(const p::View&, const p::Init&);
vector<string> decide_mineA(const p::View&, const p::Init&);
vector<string> decide_mineB(const p::View&, const p::Init&);
vector<string> decide_v5A(const p::View&, const p::Init&);
vector<string> decide_v5B(const p::View&, const p::Init&);
vector<string> decide_v6A(const p::View&, const p::Init&);
vector<string> decide_v6B(const p::View&, const p::Init&);
vector<string> decide_v7A(const p::View&, const p::Init&);
vector<string> decide_v7B(const p::View&, const p::Init&);
vector<string> decide_v8A(const p::View&, const p::Init&);
vector<string> decide_v8B(const p::View&, const p::Init&);
vector<string> decide_v9A(const p::View&, const p::Init&);
vector<string> decide_v9B(const p::View&, const p::Init&);
vector<string> decide_v10A(const p::View&, const p::Init&);
vector<string> decide_v10B(const p::View&, const p::Init&);
vector<string> decide_v12A(const p::View&, const p::Init&);
vector<string> decide_v12B(const p::View&, const p::Init&);
vector<string> decide_v13A(const p::View&, const p::Init&);
vector<string> decide_v13B(const p::View&, const p::Init&);
vector<string> decide_v14A(const p::View&, const p::Init&);
vector<string> decide_v14B(const p::View&, const p::Init&);
struct Entry { const char* name; DecideFn A, B; };
static Entry registry[] = {
  {"v15", decide_v15A, decide_v15B},
  {"v16", decide_v16A, decide_v16B},
  {"v17", decide_v17A, decide_v17B},
  {"mine", decide_mineA, decide_mineB},
  {"v5", decide_v5A, decide_v5B},
  {"v6", decide_v6A, decide_v6B},
  {"v7", decide_v7A, decide_v7B},
  {"v8", decide_v8A, decide_v8B},
  {"v9", decide_v9A, decide_v9B},
  {"v10", decide_v10A, decide_v10B},
  {"v12", decide_v12A, decide_v12B},
  {"v13", decide_v13A, decide_v13B},
  {"v14", decide_v14A, decide_v14B},
};
