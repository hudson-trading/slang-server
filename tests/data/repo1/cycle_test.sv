// Main test file that only imports base_pkg, but util_pkg should be loaded via dependencies
import base_pkg::*;

module cycle_test_module;

    // Use types from base_pkg (which exports util_pkg types)
    base_pkg::config_t system_config;
    base_pkg::result_t operation_result;  // This should resolve via export
    base_pkg::data_width_t bus_width;

    initial begin
        // Initialize using exported functions (should work via exports)
        system_config = base_pkg::create_config(32);
        bus_width = system_config.max_width;

        // Test function calls (base_pkg should have access to util_pkg functions)
        operation_result = base_pkg::validate_config(system_config);

        // Test exported enum values (accessed via base_pkg exports)
        if (operation_result == base_pkg::SUCCESS) begin
            $display("Configuration valid");
        end else if (operation_result == base_pkg::ERROR_CODE) begin
            $display("Configuration error");
        end

        $finish;
    end

endmodule
