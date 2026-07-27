#include "ccls_asp.h"

#include <stdio.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: native-c-abi-smoke WORKSPACE\n");
    return 2;
  }
  if (ccls_asp_abi_version() != CCLS_ASP_ABI_VERSION_V1) {
    fprintf(stderr, "unexpected C ABI version\n");
    return 1;
  }

  ccls_asp_result_v1 *result = ccls_asp_parse_translation_unit_with_args_v1(argv[1], "widget.cpp", "cpp", NULL, 0);
  if (result == NULL) {
    fprintf(stderr, "native parser allocation failed\n");
    return 1;
  }

  const size_t facts = ccls_asp_result_fact_count_v1(result);
  const size_t contexts = ccls_asp_result_compile_context_count_v1(result);
  const size_t errors = ccls_asp_result_error_count_v1(result);
  ccls_asp_fact_v1 first_fact = {0};
  const uint8_t has_first_fact = ccls_asp_result_fact_at_v1(result, 0, &first_fact);

  printf("C_ABI\tversion=%u\tfacts=%zu\tcontexts=%zu\terrors=%zu"
         "\tproviderProcessLaunches=0\n",
         ccls_asp_abi_version(), facts, contexts, errors);

  const int failed =
      facts == 0 || contexts != 1 || errors != 0 || has_first_fact == 0 || first_fact.qualified_name == NULL;
  ccls_asp_result_free_v1(result);
  return failed ? 1 : 0;
}
