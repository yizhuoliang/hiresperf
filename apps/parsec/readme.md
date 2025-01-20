The code are pull from https://github.com/cirosantilli/parsec-benchmark.git, note that priceton themselves no longer host the code.

In this part, the key is to use the `clang.bldconfig` file to configure the `parsecmgmt` when invoking `parsecmgmt -a build -p <program_name>`. A trick is to replace the default `gcc.bldconfig` with our clang version, so this surpasses some checks, and can work for building most programs.

Streamcluster works well directly, though. But most programs got various non-trivial errors that need handling, or even partial rewrite of the program logic (due to compatibility with clang, mainly. also the code is super old).