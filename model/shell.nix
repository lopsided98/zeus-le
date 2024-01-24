{ mkShell, python3 }: let
    python3Env = python3.withPackages (p: with p; [
        jupyter
        matplotlib
        numpy
        scipy
        sympy
    ]);
in mkShell {
    name = "zeus-le-model";

    nativeBuildInputs = [ python3Env ];
}