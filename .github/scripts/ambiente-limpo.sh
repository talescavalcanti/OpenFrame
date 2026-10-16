# Trecho comum da conferência e do teste de som (source): monta em "limpo" um
# env sem as variáveis do Qt da CI (install-qt-action) e das bibliotecas, para
# o app do pacote rodar como no computador do usuário. O PATH fica com quem chama.
#
# uso: source ambiente-limpo.sh; "${limpo[@]}" PATH=<mínimo> VAR=... <app>
limpo=(env)
for v in $(compgen -e); do
    if [[ $v =~ ^(QT_|QML|Qt[56]_|LD_LIBRARY_PATH$|PKG_CONFIG_PATH$|OPENSSL_|SSL_CERT_) ]]; then limpo+=(-u "$v"); fi
done
