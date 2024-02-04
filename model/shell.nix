{ mkShell, python3 }: let
    python3Env = python3.withPackages (p: with p; [
        jupyter
        matplotlib
        ipympl
        numpy
        scipy
        sympy
        black
    ] ++ black.optional-dependencies.jupyter);

in mkShell {
    name = "zeus-le-model";

    nativeBuildInputs = [ python3Env ];
}