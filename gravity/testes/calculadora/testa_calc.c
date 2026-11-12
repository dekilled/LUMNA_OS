/* O motor da calculadora: uma tabela de contas com o resultado esperado e, com --stdin, uma linha por conta (para o teste contra o Python). */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "calc.h"

static int total, falhas;

static void caso(const char *conta, enum calc_estado esperado, const char *texto) {
	struct calc_res r;
	enum calc_estado e = calc_avaliar(conta, &r);
	total++;
	if (e != esperado || (esperado == CALC_OK && strcmp(r.texto, texto))) {
		fprintf(stderr, "FALHOU: \"%s\": esperava %d \"%s\", veio %d \"%s\" (erro: %s)\n", conta, esperado, texto ? texto : "", e, r.texto, r.erro);
		falhas++;
	}
}

#define OK(c, t) caso(c, CALC_OK, t)

static void erro_com(const char *conta, const char *trecho) {
	struct calc_res r;
	enum calc_estado e = calc_avaliar(conta, &r);
	total++;
	if (e != CALC_ERRO || !strstr(r.erro, trecho)) {
		fprintf(stderr, "FALHOU: \"%s\": esperava erro com \"%s\", veio %d \"%s\" / \"%s\"\n", conta, trecho, e, r.texto, r.erro);
		falhas++;
	}
}

int main(int argc, char **argv) {
	if (argc > 1 && !strcmp(argv[1], "--stdin")) {
		char linha[1024];
		while (fgets(linha, sizeof(linha), stdin)) {
			linha[strcspn(linha, "\n")] = '\0';
			struct calc_res r;
			enum calc_estado e = calc_avaliar(linha, &r);
			if (e == CALC_OK) {
				printf("%.17g\n", r.valor);
			} else {
				printf("ERRO %d\n", e);
			}
		}
		return 0;
	}
	/* o esboço do Eric */
	OK("128 × 3", "384");
	OK("128*3", "384");
	OK("250 ÷ 5", "50");
	OK("250/5", "50");
	OK("89,90 + 39,90", "129,80");
	/* o básico */
	OK("2+2", "4");
	OK("2 + 3 × 4", "14");
	OK("(2 + 3) × 4", "20");
	OK("2(3+4)", "14");
	OK("10 - 3 - 2", "5");
	OK("100 / 8", "12,5");
	OK("1/3", "0,3333333333");
	OK("2/3", "0,6666666667");
	OK("0,1 + 0,2", "0,3");
	OK("0.1 + 0.2", "0,3");
	OK("2^10", "1.024");
	OK("2^3^2", "512");
	OK("-2^2", "-4");
	OK("2^-1", "0,5");
	OK("--5", "5");
	OK("-(3+4)", "-7");
	OK("7 mod 3", "1");
	OK("3 x 4", "12");
	OK("3 vezes 4", "12");
	OK("raiz(81)", "9");
	OK("√81", "9");
	OK("√(16 + 9)", "5");
	OK("raiz(2)", "1,4142135624");
	OK("pi", "3,1415926536");
	OK("2 × π", "6,2831853072");
	OK("2 + 2 =", "4");
	/* números */
	OK("1.234,56 × 2", "2.469,12");
	OK("1.234.567 + 1", "1.234.568");
	OK("1.5 × 2", "3");
	OK("1000000 × 1000", "1.000.000.000");
	OK("0,5", "0,5");
	OK(",5 + ,5", "1");
	OK("12345678 × 1000000", "12.345.678.000.000");
	OK("10 ^ 20", "1e+20");
	OK("1 / 3000000000000", "3,333333333e-13");
	OK("0 × 5", "0");
	OK("-0", "0");
	OK("5 - 5", "0");
	OK("1 - 3", "-2");
	OK("89,90 + 39,90 + 0,1", "129,90");
	OK("15,5 × 2", "31");
	OK("15,50 × 2", "31,00");
	/* porcentagem */
	OK("15% de 89,90", "13,485");
	OK("10% de 200", "20");
	OK("200 + 10%", "220");
	OK("200 - 10%", "180");
	OK("200 × 10%", "20");
	OK("10%", "10%");
	OK("50% + 25%", "75%");
	OK("1000 + 5% + 5%", "1.102,5");
	OK("200 + 10% × 3", "200,3");
	OK("(200 + 10%) × 2", "440");
	OK("12,5%", "12,5%");
	/* unidades */
	OK("5 km", "5 km");
	OK("5 km em m", "5.000 m");
	OK("5 km em milhas", "3,1068559612 mi");
	OK("1 mi para km", "1,609344 km");
	OK("100 cm em m", "1 m");
	OK("5 km + 300 m", "5,3 km");
	OK("5 km e 300 m", "5,3 km");
	OK("2 × 5 km", "10 km");
	OK("5 km × 2", "10 km");
	OK("10 km / 4", "2,5 km");
	OK("10 km / 5 km", "2");
	OK("1 kg em g", "1.000 g");
	OK("2 lb em kg", "0,90718474 kg");
	OK("1 litro em ml", "1.000 ml");
	OK("1 galão em litros", "3,785411784 L");
	OK("1 galao em litros", "3,785411784 L");
	OK("2 GB em MB", "2.000 MB");
	OK("1 GiB em MiB", "1.024 MiB");
	OK("1 TB em GB", "1.000 GB");
	OK("100 °C em °F", "212 °F");
	OK("32 °F em °C", "0 °C");
	OK("0 °C em K", "273,15 K");
	OK("0 °C em kelvin", "273,15 K");
	OK("100 graus celsius em fahrenheit", "212 °F");
	OK("-40 °C em °F", "-40 °F");
	OK("90 km/h em m/s", "25 m/s");
	OK("10 m/s em km/h", "36 km/h");
	OK("60 mph em km/h", "96,56064 km/h");
	/* tempo */
	OK("3h20", "3 h 20 min");
	OK("3h20 × 4", "13 h 20 min");
	OK("3 horas e 20 min", "3 h 20 min");
	OK("3 h 20 min + 40 min", "4 h");
	OK("90 min em h", "1,5 h");
	OK("1,5 h", "1,5 h");
	OK("2 h em min", "120 min");
	OK("1 dia em horas", "24 h");
	OK("1 semana em dias", "7 d");
	OK("1h30 - 45min", "45 min");
	OK("100 s em min", "1,6666666667 min");
	/* incompleto: a pessoa ainda está digitando */
	caso("", CALC_VAZIO, "");
	caso("   ", CALC_VAZIO, "");
	caso("128 ×", CALC_INCOMPLETO, "");
	caso("(3 + 4", CALC_INCOMPLETO, "");
	caso("raiz(", CALC_INCOMPLETO, "");
	caso("5 km em", CALC_INCOMPLETO, "");
	caso("2 +", CALC_INCOMPLETO, "");
	caso("-", CALC_INCOMPLETO, "");
	/* erros com motivo */
	erro_com("1 / 0", "divisão por zero");
	erro_com("5 km / 0", "divisão por zero");
	erro_com("7 mod 0", "divisão por zero");
	erro_com("raiz(-4)", "negativo");
	erro_com("5 km + 3 kg", "somar");
	erro_com("5 km + 3", "somar");
	erro_com("5 km em kg", "não converte");
	erro_com("5 em km", "unidade");
	erro_com("5 km em banana", "não conheço");
	erro_com("5 km × 3 kg", "multiplicar");
	erro_com("20 °C + 5 °C", "temperatura");
	erro_com("abc", "não entendi");
	erro_com("2 $ 3", "não entendi");
	erro_com("2 3", "operador");
	erro_com("2 + * 3", "não cabe");
	erro_com("(2 + 3))", "não cabe");
	erro_com("1,2,3", "vírgula");
	caso("2^0,5 × raiz", CALC_INCOMPLETO, "");
	erro_com("5 / 2 km", "dividir");
	erro_com("5 km^2", "potência");
	erro_com("(-8)^0,5", "real");
	erro_com("10 ^ 400", "grande demais");
	/* conta comprida e lixo não derrubam */
	char longa[2000];
	memset(longa, '1', sizeof(longa) - 1);
	longa[sizeof(longa) - 1] = '\0';
	struct calc_res r;
	calc_avaliar(longa, &r);
	char muitas[1200];
	for (int i = 0; i < 600; i++) {
		memcpy(muitas + 2 * i, "1+", 2);
	}
	muitas[1200 - 1] = '\0';
	calc_avaliar(muitas, &r);
	calc_avaliar("\xC3", &r);
	calc_avaliar("\xE2\x88", &r);
	calc_avaliar("5 \xC2\xB0", &r);
	calc_avaliar("((((((((((((((((((((1))))))))))))))))))))", &r);
	total += 5;
	/* embelezar */
	char b[64];
	calc_embelezar("128*3", b, sizeof(b));
	total++;
	if (strcmp(b, "128 \xC3\x97" " 3")) {
		fprintf(stderr, "FALHOU: embelezar 128*3 → %s\n", b);
		falhas++;
	}
	calc_embelezar("90 km/h em m/s", b, sizeof(b));
	total++;
	if (strcmp(b, "90 km/h em m/s")) {
		fprintf(stderr, "FALHOU: embelezar km/h → %s\n", b);
		falhas++;
	}
	calc_embelezar("85/4", b, sizeof(b));
	total++;
	if (strcmp(b, "85 \xC3\xB7" " 4")) {
		fprintf(stderr, "FALHOU: embelezar 85/4 → %s\n", b);
		falhas++;
	}
	calc_embelezar("2 * 3", b, sizeof(b));
	total++;
	if (strcmp(b, "2 \xC3\x97" " 3")) {
		fprintf(stderr, "FALHOU: embelezar não pode dobrar espaços: %s\n", b);
		falhas++;
	}
	if (falhas) {
		fprintf(stderr, "%d de %d verificações falharam\n", falhas, total);
		return 1;
	}
	printf("calculadora (motor): ok (%d verificações)\n", total);
	return 0;
}
