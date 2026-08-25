# From https://github.com/the-nix-way/dev-templates/blob/main/c-cpp/flake.nix
{
	description = "A Nix-flake-based C/C++ development environment";

	inputs.nixpkgs.url = "https://flakehub.com/f/NixOS/nixpkgs/0"; # stable Nixpkgs

	outputs = {self, ...} @ inputs: let
		supportedSystems = [
			"x86_64-linux"
			"aarch64-linux"
			"aarch64-darwin"
		];
		forEachSupportedSystem = f:
			inputs.nixpkgs.lib.genAttrs supportedSystems (
				system:
					f {
						inherit system;
						pkgs = import inputs.nixpkgs {inherit system;};
					}
			);
	in {
		devShells =
			forEachSupportedSystem (
				{
					pkgs,
					system,
				}: let
					llvmPkgs = pkgs.llvmPackages_22;
				in {
					default =
						pkgs.mkShell.override
						{
							stdenv = llvmPkgs.stdenv;
						}
						{
							packages = with pkgs;
								[
									llvmPkgs.clang-tools
									cmake
									doxygen
									ninja
									python3
									boost
									fmt
									catch2_3
									uv
								]
								++ lib.optionals (!llvmPkgs.stdenv.hostPlatform.isDarwin) [gdb];
						};
				}
			);

		formatter = forEachSupportedSystem ({pkgs, ...}: pkgs.nixfmt);
	};
}
