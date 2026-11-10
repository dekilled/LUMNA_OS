#define _GNU_SOURCE
#include "loja_hw.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>

/* ---------------- detectar ---------------- */

static int geracao_nvidia(const char *nome, const char *chip) {
	/* o código do chip (GA106, GP104, GK110…) manda; sem ele, o nome comercial */
	if (chip && chip[0]) {
		static const char *const NOVOS[] = { "TU", "GA", "AD", "GB", NULL };
		static const char *const MEIOS[] = { "GP", "GM", "GV", NULL };
		static const char *const VELHOS[] = { "GK", "GF", "G9", "G8", "GT", NULL };
		for (int i = 0; NOVOS[i]; i++) {
			if (g_str_has_prefix(chip, NOVOS[i])) {
				return 3;
			}
		}
		for (int i = 0; MEIOS[i]; i++) {
			if (g_str_has_prefix(chip, MEIOS[i])) {
				return 2;
			}
		}
		for (int i = 0; VELHOS[i]; i++) {
			if (g_str_has_prefix(chip, VELHOS[i])) {
				return 1;
			}
		}
	}
	if (strcasestr(nome, "RTX") || strcasestr(nome, "GTX 16")) {
		return 3;
	}
	if (strcasestr(nome, "GTX 10") || strcasestr(nome, "GTX 9") || strcasestr(nome, "TITAN X") || strcasestr(nome, "GTX 750")) {
		return 2;
	}
	if (strcasestr(nome, "GTX") || strcasestr(nome, "GeForce GT")) {
		return 1;
	}
	return 0;
}

static void ler_gpus(lh_hw *h) {
	char *argv[] = { "lspci", "-nn", NULL };
	char *saida = NULL;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) || !saida) {
		g_free(saida);
		return;
	}
	char **ls = g_strsplit(saida, "\n", -1);
	for (char **l = ls; *l && h->ngpu < 4; l++) {
		if (!strstr(*l, "VGA compatible controller") && !strstr(*l, "3D controller") && !strstr(*l, "Display controller")) {
			continue;
		}
		/* ... : NVIDIA Corporation GA106 [GeForce RTX 3060] [10de:2503] (rev a1) */
		char *id = strrchr(*l, '[');
		char *fechou = id ? strchr(id, ']') : NULL;
		lh_gpu *g = &h->gpus[h->ngpu];
		memset(g, 0, sizeof(*g));
		while (id && fechou && !(strlen(id) >= 11 && id[5] == ':' && fechou - id == 10)) {   /* [vvvv:dddd] */
			*id = '\0';
			id = strrchr(*l, '[');
			fechou = id ? strchr(id, ']') : NULL;
		}
		if (!id || !fechou) {
			continue;
		}
		g_strlcpy(g->pci, id + 1, 10);
		const char *v = id + 1;
		g_strlcpy(g->vendor, g_str_has_prefix(v, "10de") ? "nvidia" : g_str_has_prefix(v, "1002") ? "amd" : g_str_has_prefix(v, "8086") ? "intel" : "outro", sizeof(g->vendor));
		*id = '\0';   /* o resto é "... : Fabricante CHIP [Nome comercial] " */
		char *dp = strstr(*l, "]: ");
		char *texto = g_strstrip(g_strdup(dp ? dp + 3 : *l));
		char chipbuf[16] = "";
		const char *chip = NULL;
		char *ab = strrchr(texto, '[');
		char *fe = ab ? strrchr(texto, ']') : NULL;
		if (ab && fe && fe > ab) {
			char *antes = g_strndup(texto, (gsize)(ab - texto));
			g_strstrip(antes);
			char *sp = strrchr(antes, ' ');
			g_strlcpy(chipbuf, sp ? sp + 1 : antes, sizeof(chipbuf));
			chip = chipbuf;
			g_free(antes);
			*fe = '\0';
			g_strlcpy(g->nome, ab + 1, sizeof(g->nome));
		} else {
			g_strlcpy(g->nome, texto, sizeof(g->nome));
		}
		g_free(texto);
		if (!strcmp(g->vendor, "nvidia")) {
			g->geracao = geracao_nvidia(g->nome, chip);
		}
		h->ngpu++;
	}
	g_strfreev(ls);
	g_free(saida);
}

static void ler_cpu(lh_hw *h) {
	const char *arq = g_getenv("LUMNA_CPUINFO");
	char *t = NULL;
	if (!g_file_get_contents(arq && *arq ? arq : "/proc/cpuinfo", &t, NULL, NULL)) {
		return;
	}
	char **ls = g_strsplit(t, "\n", -1);
	char vend[32] = "";
	for (char **l = ls; *l; l++) {
		char *dp = strchr(*l, ':');
		if (!dp) {
			continue;
		}
		if (g_str_has_prefix(*l, "model name") && !h->cpu[0]) {
			g_strlcpy(h->cpu, g_strstrip(dp + 1), sizeof(h->cpu));
		} else if (g_str_has_prefix(*l, "vendor_id") && !vend[0]) {
			g_strlcpy(vend, g_strstrip(dp + 1), sizeof(vend));
		}
	}
	g_strlcpy(h->cpu_vendor, !strcmp(vend, "AuthenticAMD") ? "amd" : !strcmp(vend, "GenuineIntel") ? "intel" : "outro", sizeof(h->cpu_vendor));
	g_strfreev(ls);
	g_free(t);
}

lh_hw *lh_detectar(void) {
	lh_hw *h = g_new0(lh_hw, 1);
	ler_gpus(h);
	ler_cpu(h);
	struct utsname u;
	if (uname(&u) == 0) {
		g_strlcpy(h->arch, u.machine, sizeof(h->arch));
		g_strlcpy(h->kernel_uname, u.release, sizeof(h->kernel_uname));
	}
	const char *fake = g_getenv("LUMNA_UNAME_R");
	if (fake && *fake) {
		g_strlcpy(h->kernel_uname, fake, sizeof(h->kernel_uname));
	}
	const char *r = h->kernel_uname;
	h->kernel_pkg[0] = '\0';
	if (g_str_has_suffix(r, "-lts")) {
		g_strlcpy(h->kernel_pkg, "linux-lts", sizeof(h->kernel_pkg));
	} else if (g_str_has_suffix(r, "-zen")) {
		g_strlcpy(h->kernel_pkg, "linux-zen", sizeof(h->kernel_pkg));
	} else if (g_str_has_suffix(r, "-hardened")) {
		g_strlcpy(h->kernel_pkg, "linux-hardened", sizeof(h->kernel_pkg));
	} else if (strstr(r, "-arch")) {
		g_strlcpy(h->kernel_pkg, "linux", sizeof(h->kernel_pkg));
	}
	const char *efi = g_getenv("LUMNA_EFI");
	h->uefi = g_file_test(efi && *efi ? efi : "/sys/firmware/efi", G_FILE_TEST_IS_DIR);
	const char *boot = g_getenv("LUMNA_BOOT");
	boot = boot && *boot ? boot : "/boot";
	char *g1 = g_build_filename(boot, "grub", "grub.cfg", NULL), *s1 = g_build_filename(boot, "loader", "loader.conf", NULL);
	if (g_file_test(g1, G_FILE_TEST_EXISTS)) {
		g_strlcpy(h->boot_nome, "grub", sizeof(h->boot_nome));
	} else if (g_file_test(s1, G_FILE_TEST_EXISTS)) {
		g_strlcpy(h->boot_nome, "systemd-boot", sizeof(h->boot_nome));
	}
	g_free(g1);
	g_free(s1);
	struct statvfs sv;
	h->boot_livre = statvfs(boot, &sv) == 0 ? (int64_t)sv.f_bavail * (int64_t)sv.f_frsize : -1;
	const char *livre = g_getenv("LUMNA_BOOT_LIVRE");   /* só nos testes */
	if (livre && *livre) {
		h->boot_livre = atoll(livre);
	}
	char *mi = NULL;
	if (g_file_get_contents("/proc/meminfo", &mi, NULL, NULL)) {
		const char *p = strstr(mi, "MemTotal:");
		h->ram = p ? (int64_t)atoll(p + 9) * 1024 : 0;
		g_free(mi);
	}
	return h;
}

void lh_liberar(lh_hw *h) {
	g_free(h);
}

const char *lh_nome_compat(enum lh_compat c) {
	return c == LH_COMPATIVEL ? "Compatível" : c == LH_VERIFIQUE ? "Verifique" : "Incompatível";
}

/* ---------------- a checagem ---------------- */

static bool tem_gpu(const lh_hw *h, const char *vendor) {
	for (int i = 0; i < h->ngpu; i++) {
		if (!strcmp(h->gpus[i].vendor, vendor)) {
			return true;
		}
	}
	return false;
}

/* a melhor (maior) geração entre as placas NVIDIA; 0 se não sabe */
static int geracao_nv(const lh_hw *h) {
	int g = -1;
	for (int i = 0; i < h->ngpu; i++) {
		if (!strcmp(h->gpus[i].vendor, "nvidia") && h->gpus[i].geracao > g) {
			g = h->gpus[i].geracao;
		}
	}
	return g < 0 ? 0 : g;
}

static void marca(lh_item *it, enum lh_compat c, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void marca(lh_item *it, enum lh_compat c, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(it->motivo, sizeof(it->motivo), fmt, ap);
	va_end(ap);
	it->compat = c;
}

static bool instalado(const lj_catalogo *c, const char *p) {
	return lj_versao_instalada(c, p) != NULL;
}

static void checar_video(const lh_hw *h, const lj_catalogo *c, lh_item *it) {
	const char *p = it->pacote;
	bool amd = tem_gpu(h, "amd"), intel = tem_gpu(h, "intel"), nv = tem_gpu(h, "nvidia");
	if (!strcmp(p, "mesa") || !strcmp(p, "lib32-mesa")) {
		if (h->ngpu == 0) {
			marca(it, LH_VERIFIQUE, "Não achei nenhuma placa de vídeo (lspci). Em máquina virtual, o mesa é o que serve.");
		} else {
			marca(it, LH_COMPATIVEL, "A base gráfica de código aberto: serve a todas as placas daqui.");
		}
		return;
	}
	if (!strcmp(p, "vulkan-radeon") || !strcmp(p, "libva-mesa-driver") || !strcmp(p, "lib32-vulkan-radeon")) {
		if (!amd) {
			it->nao_aplica = true;
			marca(it, LH_INCOMPATIVEL, "Este computador não tem placa AMD.");
		} else {
			marca(it, LH_COMPATIVEL, "Você tem uma placa AMD (%s).", h->gpus[0].nome);
		}
		return;
	}
	if (!strcmp(p, "vulkan-intel") || !strcmp(p, "intel-media-driver") || !strcmp(p, "lib32-vulkan-intel")) {
		if (!intel) {
			it->nao_aplica = true;
			marca(it, LH_INCOMPATIVEL, "Este computador não tem vídeo Intel.");
		} else if (!strcmp(p, "intel-media-driver")) {
			marca(it, LH_VERIFIQUE, "Serve ao vídeo Intel da Broadwell (2014) em diante; nos mais antigos, o libva-intel-driver. Confira o seu modelo.");
		} else {
			marca(it, LH_COMPATIVEL, "Você tem vídeo Intel.");
		}
		return;
	}
	/* NVIDIA */
	if (!nv) {
		it->nao_aplica = true;
		marca(it, LH_INCOMPATIVEL, "Este computador não tem placa NVIDIA.");
		return;
	}
	int g = geracao_nv(h);
	if (!strcmp(p, "vulkan-nouveau")) {
		if (g >= 3) {
			marca(it, LH_COMPATIVEL, "O Vulkan (NVK) do driver de código aberto serve da geração Turing (RTX 20, GTX 16) em diante.");
		} else if (g == 0) {
			marca(it, LH_VERIFIQUE, "Não identifiquei a geração da placa; o NVK só serve da Turing em diante.");
		} else {
			marca(it, LH_INCOMPATIVEL, "O NVK só serve da geração Turing em diante, e a sua placa é mais antiga.");
		}
		return;
	}
	if (g == 1 || g == 2) {
		marca(it, LH_INCOMPATIVEL, g == 2 ? "Sua placa (Maxwell, Pascal ou Volta) já não é suportada pelo driver atual da NVIDIA. Use o nouveau (pacote mesa) ou o driver legado nvidia-580xx, do AUR."
			: "Sua placa é antiga demais para o driver atual da NVIDIA. Use o nouveau (pacote mesa).");
		return;
	}
	if (g == 0) {
		marca(it, LH_VERIFIQUE, "Não identifiquei a geração da placa NVIDIA. Confira no site da NVIDIA se ela é suportada pelo driver atual.");
		return;
	}
	/* Turing ou mais nova */
	if (!strcmp(p, "nvidia-utils") || !strcmp(p, "nvidia-settings") || !strcmp(p, "lib32-nvidia-utils")) {
		if (!strncmp(p, "lib32-", 6) && !lj_achar(c, p)) {
			marca(it, LH_VERIFIQUE, "O repositório multilib não está ligado em /etc/pacman.conf: ligue-o para instalar bibliotecas de 32 bits.");
		} else {
			marca(it, LH_COMPATIVEL, "Acompanha o driver NVIDIA (Turing ou mais nova).");
		}
		return;
	}
	bool dkms = g_str_has_suffix(p, "-dkms");
	const char *para = !strcmp(p, "nvidia-lts") || !strcmp(p, "nvidia-open-lts") ? "linux-lts" : "linux";
	if (dkms) {
		char hdr[64];
		g_snprintf(hdr, sizeof(hdr), "%s-headers", h->kernel_pkg[0] ? h->kernel_pkg : "linux");
		if (h->kernel_pkg[0] && instalado(c, hdr)) {
			marca(it, LH_COMPATIVEL, "Compila o módulo para qualquer kernel que você tenha (os headers do %s estão instalados).", h->kernel_pkg);
		} else {
			marca(it, LH_VERIFIQUE, "Compila o módulo para o kernel em uso, e para isso precisa dos headers (%s), que serão instalados junto.", hdr);
			g_strlcpy(it->junto, hdr, sizeof(it->junto));
		}
		return;
	}
	if (!h->kernel_pkg[0]) {
		marca(it, LH_VERIFIQUE, "É pré-compilado para o kernel %s, e não reconheci o kernel em uso (%s). Se ele for outro, use o nvidia-dkms.", para, h->kernel_uname);
	} else if (!strcmp(h->kernel_pkg, para)) {
		marca(it, LH_COMPATIVEL, "Pré-compilado para o kernel %s, que você usa. Sua placa (%s) é Turing ou mais nova.", para, h->gpus[0].nome);
	} else {
		marca(it, LH_INCOMPATIVEL, "É pré-compilado para o kernel %s e você usa o %s. Use o nvidia-dkms (ou o pacote do seu kernel).", para, h->kernel_pkg);
	}
}

static void checar_cpu(const lh_hw *h, lh_item *it) {
	bool amd = !strcmp(it->pacote, "amd-ucode");
	if (strcmp(h->cpu_vendor, amd ? "amd" : "intel")) {
		it->nao_aplica = true;
		marca(it, LH_INCOMPATIVEL, "Este processador não é %s (%s).", amd ? "AMD" : "Intel", h->cpu[0] ? h->cpu : "não identifiquei");
	} else {
		marca(it, LH_COMPATIVEL, "Correções de segurança e de estabilidade para o seu processador (%s). Valem depois de reiniciar.", h->cpu);
	}
}

static void checar_kernel(const lh_hw *h, const lj_catalogo *c, lh_item *it) {
	const char *p = it->pacote;
	g_snprintf(it->junto, sizeof(it->junto), "%s-headers", p);
	it->em_uso = !strcmp(h->kernel_pkg, p);
	if (it->instalado) {
		marca(it, LH_COMPATIVEL, it->em_uso ? "É o kernel em uso (%s)." : "Já está instalado.", h->kernel_uname);
		return;
	}
	if (strcmp(h->arch, "x86_64")) {
		marca(it, LH_VERIFIQUE, "A Loja só foi testada em x86_64 e esta máquina é %s.", h->arch[0] ? h->arch : "desconhecida");
		return;
	}
	if (h->boot_livre >= 0 && h->boot_livre < 200LL * 1024 * 1024) {
		marca(it, LH_INCOMPATIVEL, "Pouco espaço em /boot: %.0f MB livres, e um kernel novo precisa de uns 200 MB (o kernel e a imagem de boot).", (double)h->boot_livre / 1048576.0);
		return;
	}
	/* o driver NVIDIA pré-compilado que já está aqui é de um kernel só */
	bool nv_pre = instalado(c, "nvidia") || instalado(c, "nvidia-open");
	bool nv_lts = instalado(c, "nvidia-lts") || instalado(c, "nvidia-open-lts");
	bool nv_dkms = instalado(c, "nvidia-dkms") || instalado(c, "nvidia-open-dkms");
	if (!nv_dkms && ((nv_pre && strcmp(p, "linux")) || (!nv_lts && !strcmp(p, "linux-lts") && (nv_pre)))) {
		marca(it, LH_VERIFIQUE, "O driver NVIDIA instalado é pré-compilado para o kernel linux. Neste kernel ele não carrega: instale também o nvidia-dkms (ou o nvidia-lts, no linux-lts), senão a tela pode ficar sem aceleração.");
		return;
	}
	if (!h->boot_nome[0]) {
		marca(it, LH_VERIFIQUE, "Não reconheci o gerenciador de boot (GRUB ou systemd-boot). Instalar é seguro, mas confira depois se a entrada do novo kernel apareceu.");
		return;
	}
	marca(it, LH_COMPATIVEL, "x86_64, %.0f MB livres em /boot, gerenciador de boot: %s. Os headers (%s) vão junto. Depois de instalar, a Loja confere a entrada do boot.", h->boot_livre >= 0 ? (double)h->boot_livre / 1048576.0 : 0.0, h->boot_nome, it->junto);
}

static const struct {
	const char *pacote, *descricao;
	enum lh_grupo grupo;
} TABELA[] = {
	{ "mesa", "OpenGL, Vulkan e OpenCL de código aberto: a base gráfica", LH_VIDEO },
	{ "vulkan-radeon", "Vulkan para Radeon", LH_VIDEO },
	{ "libva-mesa-driver", "Aceleração de vídeo (VA-API) para Radeon", LH_VIDEO },
	{ "vulkan-intel", "Vulkan para o vídeo Intel", LH_VIDEO },
	{ "intel-media-driver", "Aceleração de vídeo (VA-API) para o vídeo Intel moderno", LH_VIDEO },
	{ "nvidia-open", "Driver NVIDIA com módulo aberto (Turing ou mais nova), para o kernel linux", LH_VIDEO },
	{ "nvidia", "Driver NVIDIA proprietário, para o kernel linux", LH_VIDEO },
	{ "nvidia-lts", "Driver NVIDIA proprietário, para o kernel linux-lts", LH_VIDEO },
	{ "nvidia-dkms", "Driver NVIDIA compilado para qualquer kernel (precisa dos headers)", LH_VIDEO },
	{ "nvidia-utils", "Utilitários e bibliotecas do driver NVIDIA", LH_VIDEO },
	{ "nvidia-settings", "Painel de controle da NVIDIA", LH_VIDEO },
	{ "vulkan-nouveau", "Vulkan (NVK) do driver de código aberto da NVIDIA", LH_VIDEO },
	{ "lib32-mesa", "Mesa de 32 bits (jogos e Wine)", LH_VIDEO },
	{ "lib32-vulkan-radeon", "Vulkan de 32 bits para Radeon", LH_VIDEO },
	{ "lib32-nvidia-utils", "Bibliotecas NVIDIA de 32 bits (jogos e Wine)", LH_VIDEO },
	{ "amd-ucode", "Microcódigo do processador AMD", LH_CPU },
	{ "intel-ucode", "Microcódigo do processador Intel", LH_CPU },
	{ "linux", "Kernel padrão do Arch", LH_KERNEL },
	{ "linux-lts", "Kernel de suporte prolongado", LH_KERNEL },
	{ "linux-zen", "Kernel com ajustes de desempenho para desktop e jogos", LH_KERNEL },
	{ "linux-hardened", "Kernel com proteções extras de segurança", LH_KERNEL },
};

int lh_itens(const lh_hw *h, const lj_catalogo *c, lh_item *saida, int max) {
	int n = 0;
	for (size_t i = 0; i < G_N_ELEMENTS(TABELA) && n < max; i++) {
		lh_item *it = &saida[n++];
		memset(it, 0, sizeof(*it));
		g_strlcpy(it->pacote, TABELA[i].pacote, sizeof(it->pacote));
		g_strlcpy(it->exibicao, TABELA[i].pacote, sizeof(it->exibicao));
		g_strlcpy(it->descricao, TABELA[i].descricao, sizeof(it->descricao));
		it->grupo = TABELA[i].grupo;
		it->instalado = instalado(c, it->pacote);
		const lj_pacote *p = lj_achar(c, it->pacote);
		if (p) {
			g_strlcpy(it->repo, p->repo, sizeof(it->repo));
		}
		if (it->grupo == LH_VIDEO) {
			checar_video(h, c, it);
		} else if (it->grupo == LH_CPU) {
			checar_cpu(h, it);
		} else {
			checar_kernel(h, c, it);
		}
		/* o que o catálogo não tem não se instala, nem com o modo avançado (lib32-* é o multilib desligado: já avisado acima) */
		if (!p && !it->instalado && strncmp(it->pacote, "lib32-", 6) != 0 && !it->nao_aplica) {
			marca(it, LH_INCOMPATIVEL, "Este pacote não está nos repositórios ligados (%s): sincronize os repositórios ou confira /etc/pacman.conf.", it->pacote);
			it->nao_aplica = false;
		}
	}
	return n;
}

bool lh_pode_instalar(const lh_item *it, bool avancado) {
	if (it->instalado) {
		return false;
	}
	return it->compat != LH_INCOMPATIVEL || avancado;
}

bool lh_pode_remover(const lh_item *it, const lh_hw *h, const lj_catalogo *c, char *motivo, size_t tam) {
	motivo[0] = '\0';
	if (!it->instalado) {
		return false;
	}
	if (it->grupo == LH_KERNEL) {
		if (it->em_uso) {
			g_strlcpy(motivo, "É o kernel em uso: remover deixaria o sistema sem ele. Reinicie em outro kernel antes.", tam);
			return false;
		}
		int n = 0;
		for (size_t i = 0; i < G_N_ELEMENTS(TABELA); i++) {
			n += TABELA[i].grupo == LH_KERNEL && instalado(c, TABELA[i].pacote);
		}
		if (n <= 1) {
			g_strlcpy(motivo, "É o único kernel instalado: o sistema não iniciaria.", tam);
			return false;
		}
		return true;
	}
	if (!strcmp(it->pacote, "mesa") && h->ngpu > 0) {
		g_strlcpy(motivo, "O mesa é a base gráfica do sistema: sem ele a tela pode não subir.", tam);
		return false;
	}
	return true;
}

/* ---------------- depois de instalar um kernel ---------------- */

int lh_verificar_kernel(const lh_hw *h, const char *pacote, lh_checagem *saida, int max) {
	const char *boot = g_getenv("LUMNA_BOOT");
	boot = boot && *boot ? boot : "/boot";
	int n = 0;
	char *vm = g_strdup_printf("%s/vmlinuz-%s", boot, pacote), *ini = g_strdup_printf("%s/initramfs-%s.img", boot, pacote);
	if (n < max) {
		saida[n].ok = g_file_test(vm, G_FILE_TEST_EXISTS);
		g_snprintf(saida[n].texto, sizeof(saida[n].texto), saida[n].ok ? "O kernel está em /boot (vmlinuz-%s)." : "Faltou o kernel em /boot (vmlinuz-%s): a instalação não terminou bem.", pacote);
		n++;
	}
	if (n < max) {
		saida[n].ok = g_file_test(ini, G_FILE_TEST_EXISTS);
		g_snprintf(saida[n].texto, sizeof(saida[n].texto), saida[n].ok ? "A imagem de boot foi gerada (initramfs-%s.img)." : "Faltou a imagem de boot (initramfs-%s.img): rode `sudo mkinitcpio -P` e confira o erro.", pacote);
		n++;
	}
	if (n < max) {
		lh_hw copia = *h;
		bool tem = false;
		if (!strcmp(h->boot_nome, "grub")) {
			char *cfg = g_strdup_printf("%s/grub/grub.cfg", boot), *txt = NULL;
			char *procurado = g_strdup_printf("vmlinuz-%s", pacote);
			tem = g_file_get_contents(cfg, &txt, NULL, NULL) && strstr(txt, procurado);
			g_free(txt);
			g_free(cfg);
			g_free(procurado);
			saida[n].ok = tem;
			g_snprintf(saida[n].texto, sizeof(saida[n].texto), tem ? "O GRUB já conhece este kernel." : "O GRUB ainda não tem este kernel no menu. Rode: sudo grub-mkconfig -o /boot/grub/grub.cfg");
		} else if (!strcmp(h->boot_nome, "systemd-boot")) {
			char *dir = g_strdup_printf("%s/loader/entries", boot);
			GDir *d = g_dir_open(dir, 0, NULL);
			const char *nome;
			char *procurado = g_strdup_printf("vmlinuz-%s", pacote);
			while (d && (nome = g_dir_read_name(d))) {
				char *arq = g_build_filename(dir, nome, NULL), *txt = NULL;
				if (g_file_get_contents(arq, &txt, NULL, NULL) && strstr(txt, procurado)) {
					tem = true;
				}
				g_free(txt);
				g_free(arq);
			}
			if (d) {
				g_dir_close(d);
			}
			g_free(dir);
			g_free(procurado);
			saida[n].ok = tem;
			g_snprintf(saida[n].texto, sizeof(saida[n].texto), tem ? "O systemd-boot já tem uma entrada para este kernel." : "Falta a entrada do systemd-boot: copie a de /boot/loader/entries trocando vmlinuz-linux e initramfs-linux por vmlinuz-%s e initramfs-%s.img.", pacote, pacote);
		} else {
			saida[n].ok = false;
			g_snprintf(saida[n].texto, sizeof(saida[n].texto), "Não reconheci o gerenciador de boot: confira você mesmo se o kernel %s aparece no menu antes de reiniciar.", pacote);
		}
		(void)copia;
		n++;
	}
	g_free(vm);
	g_free(ini);
	return n;
}
