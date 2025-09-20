#include "security/secure_validator_identity.h"
#include <iostream>
#include <string>
#include <vector>

using namespace slonana::security;

void print_usage() {
    std::cout << "Slonana Key Management CLI\n";
    std::cout << "Usage: slonana-keygen [command] [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  generate          Generate a new validator identity key\n";
    std::cout << "  import <path>     Import legacy keypair from file\n";
    std::cout << "  rotate            Rotate the primary validator identity\n";
    std::cout << "  status            Show status of current identity\n";
    std::cout << "  list              List all validator identities\n";
    std::cout << "  export <path>     Export primary identity to legacy format\n";
    std::cout << "  revoke <reason>   Revoke the primary identity\n";
    std::cout << "  audit             Show audit information for all keys\n";
    std::cout << "  cleanup           Clean up expired and revoked keys\n\n";
    std::cout << "Options:\n";
    std::cout << "  --storage-path <path>  Key storage directory (default: ~/.slonana/keys)\n";
    std::cout << "  --help                 Show this help message\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];
    std::string storage_path = std::string(getenv("HOME") ? getenv("HOME") : ".") + "/.slonana/keys";
    
    // Parse options
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--storage-path" && i + 1 < argc) {
            storage_path = argv[++i];
        } else if (arg == "--help") {
            print_usage();
            return 0;
        }
    }

    // Create secure validator identity
    auto identity = SecureKeyManagerFactory::create_secure_identity(storage_path);
    auto init_result = identity->initialize();
    if (!init_result.is_ok()) {
        std::cerr << "Failed to initialize key manager: " << init_result.error() << std::endl;
        return 1;
    }

    if (command == "generate") {
        std::cout << "Generating new validator identity...\n";
        
        auto result = identity->create_new_identity();
        if (result.is_ok()) {
            std::cout << "✅ Generated new validator identity: " << result.value() << std::endl;
            
            auto pub_key_result = identity->get_public_key();
            if (pub_key_result.is_ok()) {
                const auto& pub_key = pub_key_result.value();
                std::cout << "Public key: ";
                for (size_t i = 0; i < std::min(pub_key.size(), size_t(16)); ++i) {
                    printf("%02x", pub_key[i]);
                }
                std::cout << "..." << std::endl;
            }
        } else {
            std::cerr << "❌ Failed to generate identity: " << result.error() << std::endl;
            return 1;
        }
        
    } else if (command == "import") {
        if (argc < 3) {
            std::cerr << "❌ Import command requires a file path\n";
            return 1;
        }
        
        std::string file_path = argv[2];
        std::cout << "Importing legacy keypair from " << file_path << "...\n";
        
        auto result = identity->import_legacy_identity(file_path);
        if (result.is_ok()) {
            std::cout << "✅ Imported validator identity: " << result.value() << std::endl;
        } else {
            std::cerr << "❌ Failed to import identity: " << result.error() << std::endl;
            return 1;
        }
        
    } else if (command == "rotate") {
        std::cout << "Rotating primary validator identity...\n";
        
        auto result = identity->rotate_identity();
        if (result.is_ok()) {
            std::cout << "✅ Rotated to new identity: " << result.value() << std::endl;
        } else {
            std::cerr << "❌ Failed to rotate identity: " << result.error() << std::endl;
            return 1;
        }
        
    } else if (command == "status") {
        auto result = identity->get_status();
        if (result.is_ok()) {
            const auto& status = result.value();
            std::cout << "Primary Validator Identity Status:\n";
            std::cout << "  Key ID: " << status.key_id << "\n";
            std::cout << "  Type: " << status.key_type << "\n";
            std::cout << "  Created: " << std::chrono::duration_cast<std::chrono::seconds>(
                status.created_at.time_since_epoch()).count() << " (unix timestamp)\n";
            std::cout << "  Expires: " << std::chrono::duration_cast<std::chrono::seconds>(
                status.expires_at.time_since_epoch()).count() << " (unix timestamp)\n";
            std::cout << "  Use Count: " << status.use_count << "\n";
            std::cout << "  Revoked: " << (status.is_revoked ? "yes" : "no") << "\n";
            std::cout << "  Needs Rotation: " << (status.needs_rotation ? "yes" : "no") << "\n";
            
            if (status.needs_rotation) {
                std::cout << "⚠️  Key rotation recommended\n";
            } else {
                std::cout << "✅ Key is current and valid\n";
            }
        } else {
            std::cerr << "❌ Failed to get status: " << result.error() << std::endl;
            return 1;
        }
        
    } else if (command == "list") {
        auto result = identity->list_all_identities();
        if (result.is_ok()) {
            const auto& identities = result.value();
            std::cout << "All Validator Identities (" << identities.size() << "):\n\n";
            
            for (const auto& id_status : identities) {
                std::cout << "ID: " << id_status.key_id << "\n";
                std::cout << "  Type: " << id_status.key_type << "\n";
                std::cout << "  Uses: " << id_status.use_count << "\n";
                std::cout << "  Status: ";
                if (id_status.is_revoked) {
                    std::cout << "REVOKED";
                } else if (id_status.needs_rotation) {
                    std::cout << "NEEDS_ROTATION";
                } else {
                    std::cout << "ACTIVE";
                }
                std::cout << "\n";
                
                if (id_status.key_id == identity->get_primary_key_id()) {
                    std::cout << "  [PRIMARY]\n";
                }
                std::cout << "\n";
            }
        } else {
            std::cerr << "❌ Failed to list identities: " << result.error() << std::endl;
            return 1;
        }
        
    } else if (command == "export") {
        if (argc < 3) {
            std::cerr << "❌ Export command requires an output path\n";
            return 1;
        }
        
        std::string output_path = argv[2];
        std::cout << "Exporting primary identity to " << output_path << "...\n";
        
        auto result = identity->export_for_legacy_use(output_path);
        if (result.is_ok()) {
            std::cout << "✅ Exported identity to legacy format\n";
            std::cout << "⚠️  Remember to set proper file permissions: chmod 600 " << output_path << std::endl;
        } else {
            std::cerr << "❌ Failed to export identity: " << result.error() << std::endl;
            return 1;
        }
        
    } else if (command == "revoke") {
        std::string reason = "Manual revocation";
        if (argc >= 3) {
            reason = argv[2];
        }
        
        std::cout << "Revoking primary validator identity...\n";
        std::cout << "Reason: " << reason << "\n";
        
        auto result = identity->revoke_identity(reason);
        if (result.is_ok()) {
            std::cout << "✅ Identity revoked successfully\n";
            std::cout << "⚠️  Generate a new identity before starting the validator\n";
        } else {
            std::cerr << "❌ Failed to revoke identity: " << result.error() << std::endl;
            return 1;
        }
        
    } else if (command == "audit") {
        auto key_manager = identity->get_key_manager();
        if (key_manager) {
            auto audit_result = key_manager->audit_key_usage();
            if (audit_result.is_ok()) {
                const auto& audit_log = audit_result.value();
                std::cout << "Key Usage Audit Report:\n\n";
                
                for (const auto& entry : audit_log) {
                    std::cout << entry << "\n";
                }
                
                auto stats = key_manager->get_stats();
                std::cout << "\nSummary:\n";
                std::cout << "  Total Keys: " << stats.total_keys << "\n";
                std::cout << "  Active Keys: " << stats.active_keys << "\n";
                std::cout << "  Expired Keys: " << stats.expired_keys << "\n";
                std::cout << "  Revoked Keys: " << stats.revoked_keys << "\n";
            } else {
                std::cerr << "❌ Failed to generate audit report: " << audit_result.error() << std::endl;
                return 1;
            }
        }
        
    } else if (command == "cleanup") {
        std::cout << "Cleaning up expired and revoked keys...\n";
        
        auto key_manager = identity->get_key_manager();
        if (key_manager) {
            auto cleanup_result = key_manager->secure_wipe_all_revoked_keys();
            if (cleanup_result.is_ok()) {
                std::cout << "✅ Cleanup completed successfully\n";
            } else {
                std::cerr << "❌ Cleanup failed: " << cleanup_result.error() << std::endl;
                return 1;
            }
        }
        
    } else {
        std::cerr << "❌ Unknown command: " << command << std::endl;
        print_usage();
        return 1;
    }

    // Shutdown cleanly
    identity->shutdown();
    return 0;
}