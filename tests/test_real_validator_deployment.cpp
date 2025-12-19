#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

// Test that actually starts a real validator process and deploys/executes
// programs on it
class RealValidatorTest {
private:
  pid_t validator_pid_ = -1;
  std::string ledger_path_;
  std::string identity_path_;
  int rpc_port_ = 8899;

public:
  RealValidatorTest() {
    ledger_path_ = "/tmp/slonana-test-ledger";
    identity_path_ = "/tmp/slonana-test-identity.json";
  }

  ~RealValidatorTest() { cleanup(); }

  bool setup() {
    std::cout << "[SETUP] Preparing test environment..." << std::endl;

    // Create ledger directory
    std::string cmd = "mkdir -p " + ledger_path_;
    system(cmd.c_str());

    // Generate identity keypair
    cmd = "ssh-keygen -t ed25519 -f " + identity_path_ +
          " -N '' -q 2>/dev/null || true";
    system(cmd.c_str());

    return true;
  }

  bool start_validator() {
    std::cout << "[START] Starting slonana validator process..." << std::endl;

    validator_pid_ = fork();

    if (validator_pid_ == 0) {
      // Child process - run validator
      std::string validator_bin = "./slonana_validator";

      // Check if binary exists
      if (access(validator_bin.c_str(), X_OK) != 0) {
        validator_bin = "./build/slonana_validator";
      }
      if (access(validator_bin.c_str(), X_OK) != 0) {
        validator_bin = "../build/slonana_validator";
      }

      std::cout << "Executing: " << validator_bin << std::endl;

      execl(validator_bin.c_str(), "slonana_validator", "validator",
            "--ledger-path", ledger_path_.c_str(), "--identity",
            identity_path_.c_str(), "--rpc-bind-address", "127.0.0.1:8899",
            "--gossip-bind-address", "127.0.0.1:8001", nullptr);

      // If execl returns, it failed
      std::cerr << "Failed to start validator" << std::endl;
      exit(1);
    }

    if (validator_pid_ < 0) {
      std::cerr << "Failed to fork validator process" << std::endl;
      return false;
    }

    std::cout << "Validator PID: " << validator_pid_ << std::endl;
    std::cout << "RPC: http://127.0.0.1:" << rpc_port_ << std::endl;

    return true;
  }

  bool wait_for_rpc() {
    std::cout << "[WAIT] Waiting for RPC endpoint to be ready..." << std::endl;

    for (int i = 0; i < 30; i++) {
      // Try to connect to RPC
      std::string cmd =
          "curl -s -X POST -H 'Content-Type: application/json' "
          "-d '{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"getHealth\"}' "
          "http://127.0.0.1:8899 2>/dev/null | grep -q result";

      if (system(cmd.c_str()) == 0) {
        std::cout << "✓ RPC endpoint responding" << std::endl;
        return true;
      }

      // Check if validator is still running
      int status;
      pid_t result = waitpid(validator_pid_, &status, WNOHANG);
      if (result != 0) {
        std::cerr << "Validator process exited prematurely" << std::endl;
        return false;
      }

      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "." << std::flush;
    }

    std::cerr << "\n✗ RPC endpoint not ready after 30 seconds" << std::endl;
    return false;
  }

  bool deploy_program(const std::string &program_path,
                      std::string &program_id_out) {
    std::cout << "[DEPLOY] Deploying sBPF program: " << program_path
              << std::endl;

    // Read program binary
    std::ifstream file(program_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      std::cerr << "Failed to open program file: " << program_path
                << std::endl;
      return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "Program size: " << size << " bytes" << std::endl;

    // For now, use CLI-based deployment
    std::string cmd = "./scripts/slonana-cli.sh deploy " + program_path +
                      " 2>&1 | tee /tmp/deploy.log";
    int ret = system(cmd.c_str());

    if (ret != 0) {
      std::cerr << "Deployment failed" << std::endl;
      return false;
    }

    // Extract program ID from output
    cmd = "grep 'Program ID:' /tmp/deploy.log | awk '{print $3}'";
    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe) {
      char buffer[128];
      if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        program_id_out = buffer;
        // Trim newline
        program_id_out.erase(program_id_out.find_last_not_of("\n\r") + 1);
      }
      pclose(pipe);
    }

    if (program_id_out.empty()) {
      std::cerr << "Failed to extract program ID" << std::endl;
      return false;
    }

    std::cout << "✓ Program deployed: " << program_id_out << std::endl;
    return true;
  }

  bool execute_transaction(const std::string &program_id, int instruction) {
    std::cout << "[TX] Executing transaction on program: " << program_id
              << std::endl;

    std::string cmd = "./scripts/slonana-cli.sh call " + program_id + " " +
                      std::to_string(instruction);
    int ret = system(cmd.c_str());

    if (ret == 0) {
      std::cout << "✓ Transaction succeeded" << std::endl;
      return true;
    } else {
      std::cerr << "✗ Transaction failed" << std::endl;
      return false;
    }
  }

  void cleanup() {
    if (validator_pid_ > 0) {
      std::cout << "[STOP] Shutting down validator..." << std::endl;
      kill(validator_pid_, SIGTERM);

      // Wait up to 5 seconds for graceful shutdown
      for (int i = 0; i < 5; i++) {
        int status;
        pid_t result = waitpid(validator_pid_, &status, WNOHANG);
        if (result != 0) {
          std::cout << "✓ Validator stopped" << std::endl;
          validator_pid_ = -1;
          return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      // Force kill if still running
      kill(validator_pid_, SIGKILL);
      waitpid(validator_pid_, nullptr, 0);
      std::cout << "✓ Validator force stopped" << std::endl;
      validator_pid_ = -1;
    }

    // Cleanup temporary files
    system(("rm -rf " + ledger_path_).c_str());
    system(("rm -f " + identity_path_ + "*").c_str());
  }
};

int main(int argc, char *argv[]) {
  std::cout << "╔══════════════════════════════════════════════════════════════╗"
            << std::endl;
  std::cout << "║    REAL VALIDATOR DEPLOYMENT TEST - NO MOCKS                 ║"
            << std::endl;
  std::cout << "╚══════════════════════════════════════════════════════════════╝"
            << std::endl;
  std::cout << std::endl;

  RealValidatorTest test;

  // Setup
  if (!test.setup()) {
    std::cerr << "Setup failed" << std::endl;
    return 1;
  }

  // Start validator
  if (!test.start_validator()) {
    std::cerr << "Failed to start validator" << std::endl;
    return 1;
  }

  // Wait for RPC
  if (!test.wait_for_rpc()) {
    std::cerr << "RPC endpoint not ready" << std::endl;
    test.cleanup();
    return 1;
  }

  // Deploy async agent program
  std::string program_path =
      "examples/async_agent/build/async_agent_sbpf.so";
  std::string program_id;

  if (!test.deploy_program(program_path, program_id)) {
    std::cerr << "Program deployment failed" << std::endl;
    test.cleanup();
    return 1;
  }

  // Execute transactions
  bool all_passed = true;

  std::cout << std::endl;
  std::cout << "[EXECUTE] Running transaction sequence..." << std::endl;

  // TX1: Initialize
  if (!test.execute_transaction(program_id, 0)) {
    all_passed = false;
  }

  // TX2: Timer tick
  if (!test.execute_transaction(program_id, 1)) {
    all_passed = false;
  }

  // TX3: ML inference
  if (!test.execute_transaction(program_id, 3)) {
    all_passed = false;
  }

  // Cleanup
  test.cleanup();

  std::cout << std::endl;
  if (all_passed) {
    std::cout
        << "╔══════════════════════════════════════════════════════════════╗"
        << std::endl;
    std::cout
        << "║           REAL VALIDATOR TEST PASSED                         ║"
        << std::endl;
    std::cout
        << "╚══════════════════════════════════════════════════════════════╝"
        << std::endl;
    return 0;
  } else {
    std::cout
        << "╔══════════════════════════════════════════════════════════════╗"
        << std::endl;
    std::cout
        << "║           REAL VALIDATOR TEST FAILED                         ║"
        << std::endl;
    std::cout
        << "╚══════════════════════════════════════════════════════════════╝"
        << std::endl;
    return 1;
  }
}
