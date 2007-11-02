int start_ssldaemon(int count, const char *ssl_cert, const char *ssl_private_key, const char *ssl_dh_params);
void start_ssld_accept(rb_fde_t *sslF, rb_fde_t *plainF);
void start_ssld_connect(rb_fde_t *sslF, rb_fde_t *plainF);
