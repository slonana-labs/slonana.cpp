class SlonanaValidator < Formula
  desc "High-performance C++ Solana validator implementation"
  homepage "https://github.com/slonana-labs/slonana.cpp"
  url "https://github.com/slonana-labs/slonana.cpp/archive/v1.0.0.tar.gz"
  sha256 "YOUR_SHA256_HERE"  # Will be calculated during actual release
  license "MIT"
  head "https://github.com/slonana-labs/slonana.cpp.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "openssl@3"
  depends_on macos: :monterey

  def install
    # Set up build environment
    ENV.cxx11

    # Create build directory
    mkdir "build" do
      # Configure build
      system "cmake", "..", 
             "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_CXX_STANDARD=20",
             "-DOPENSSL_ROOT_DIR=#{Formula["openssl@3"].opt_prefix}",
             *std_cmake_args

      # Build
      system "make", "-j#{ENV.make_jobs}"

      # Install binaries
      bin.install "slonana_validator"
      bin.install "slonana-genesis"
    end

    # Install configuration files
    (etc/"slonana").mkpath
    (etc/"slonana/validator.toml").write validator_config

    # Create data and log directories
    (var/"lib/slonana").mkpath
    (var/"log/slonana").mkpath

    # Install documentation
    doc.install "README.md"
    doc.install "docs" if File.directory?("docs")
    
    # Install license
    prefix.install "LICENSE"
  end

  def validator_config
    <<~EOS
      # Slonana Validator Configuration for macOS
      # See docs/CONFIGURATION.md for full documentation

      [network]
      gossip_port = 8001
      rpc_port = 8899
      validator_port = 8003

      [validator]
      identity_file = "#{var}/lib/slonana/identity.json"
      vote_account_file = "#{var}/lib/slonana/vote-account.json"
      ledger_path = "#{var}/lib/slonana/ledger"

      [consensus]
      enable_voting = true
      vote_threshold = 0.67

      [rpc]
      enable_rpc = true
      rpc_bind_address = "127.0.0.1:8899"

      [monitoring]
      enable_metrics = true
      metrics_port = 9090

      [logging]
      log_level = "info"
      log_file = "#{var}/log/slonana/validator.log"
    EOS
  end

  service do
    run [opt_bin/"slonana_validator", "--config", etc/"slonana/validator.toml"]
    keep_alive true
    error_log_path var/"log/slonana/error.log"
    log_path var/"log/slonana/output.log"
    working_dir var/"lib/slonana"
  end

  test do
    # Test basic binary functionality
    assert_match "slonana_validator", shell_output("#{bin}/slonana_validator --version", 2)
    
    # Test genesis tool
    assert_match "slonana-genesis", shell_output("#{bin}/slonana-genesis --help", 2)
    
    # Test configuration file exists
    assert_predicate etc/"slonana/validator.toml", :exist?
  end

  def caveats
    <<~EOS
      Configuration file: #{etc}/slonana/validator.toml
      Data directory: #{var}/lib/slonana
      Log directory: #{var}/log/slonana

      To start slonana-validator:
        brew services start slonana-validator

      To stop slonana-validator:
        brew services stop slonana-validator

      To generate validator identity keys:
        #{bin}/slonana-genesis create-identity --output-dir #{var}/lib/slonana

      For full documentation, visit:
        https://docs.slonana.dev
    EOS
  end
end