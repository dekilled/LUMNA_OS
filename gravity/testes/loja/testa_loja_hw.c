/* O serviço de drivers e kernels: o hardware (lspci e cpuinfo falsos), a compatibilidade de cada driver e kernel, o que pode instalar ou remover e a conferência do boot. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "loja_hw.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static char *dir;

static void escreve(const char *nome, const char *txt) {
	char *c = g_build_filename(dir, nome, NULL), *pai = g_path_get_dirname(c);
	g_mkdir_with_parents(pai, 0755);
	g_file_set_contents(c, txt, -1, NULL);
	g_free(pai);
	g_free(c);
}

static void cenario(const char *lspci, const char *cpu_vendor, const char *cpu_nome, const char *uname_r, const char *instalados, const char *boot_livre) {
	escreve("lspci.txt", lspci);
	char *ci = g_strdup_printf("processor\t: 0\nvendor_id\t: %s\nmodel name\t: %s\n", cpu_vendor, cpu_nome);
	escreve("cpuinfo.txt", ci);
	g_free(ci);
	char *a = g_build_filename(dir, "lspci.txt", NULL), *b = g_build_filename(dir, "cpuinfo.txt", NULL);
	setenv("FALSO_LSPCI", a, 1);
	setenv("LUMNA_CPUINFO", b, 1);
	g_free(a);
	g_free(b);
	setenv("LUMNA_UNAME_R", uname_r, 1);
	setenv("FALSO_EXTRA", instalados, 1);
	if (boot_livre) {
		setenv("LUMNA_BOOT_LIVRE", boot_livre, 1);
	} else {
		unsetenv("LUMNA_BOOT_LIVRE");
	}
}

static const lh_item *acha(const lh_item *v, int n, const char *p) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(v[i].pacote, p)) {
			return &v[i];
		}
	}
	return NULL;
}

#define ITEM(p) acha(v, n, p)
#define QUAL(p, esperado) do { const lh_item *x_ = ITEM(p); CONFERE(x_ && x_->compat == (esperado), "%s deveria ser %s, é %s (%s)", p, lh_nome_compat(esperado), x_ ? lh_nome_compat(x_->compat) : "(sumiu)", x_ ? x_->motivo : ""); } while (0)

static const char *AMD = "00:00.0 Host bridge [0600]: Advanced Micro Devices, Inc. [AMD] Starship/Matisse Root Complex [1022:1480]\n"
	"0a:00.0 VGA compatible controller [0300]: Advanced Micro Devices, Inc. [AMD/ATI] Navi 23 [Radeon RX 6600/6600 XT/6600M] [1002:73ff] (rev c1)\n"
	"0a:00.1 Audio device [0403]: Advanced Micro Devices, Inc. [AMD/ATI] Navi 21/23 HDMI/DP Audio Controller [1002:ab28]\n";
static const char *NV3060 = "01:00.0 VGA compatible controller [0300]: NVIDIA Corporation GA106 [GeForce RTX 3060] [10de:2503] (rev a1)\n";
static const char *NV1060 = "01:00.0 VGA compatible controller [0300]: NVIDIA Corporation GP106 [GeForce GTX 1060 6GB] [10de:1c03] (rev a1)\n";
static const char *HIBRIDO = "00:02.0 VGA compatible controller [0300]: Intel Corporation Alder Lake-P GT2 [Iris Xe Graphics] [8086:46a6] (rev 0c)\n"
	"01:00.0 3D controller [0302]: NVIDIA Corporation AD107M [GeForce RTX 4060 Max-Q / Mobile] [10de:28a0] (rev a1)\n";

int main(void) {
	dir = g_dir_make_tmp("loja-hw-XXXXXX", NULL);
	setenv("LUMNA_BOOT", dir, 1);
	char *efi = g_build_filename(dir, "efi", NULL);
	g_mkdir_with_parents(efi, 0755);
	setenv("LUMNA_EFI", efi, 1);
	g_free(efi);
	escreve("grub/grub.cfg", "menuentry 'Arch' { linux /vmlinuz-linux }\n");
	setenv("FALSO_CATALOGO_EXTRA", "vulkan-radeon vulkan-intel vulkan-nouveau intel-media-driver nvidia nvidia-open nvidia-lts nvidia-dkms nvidia-utils nvidia-settings amd-ucode intel-ucode linux-zen linux-hardened linux-headers linux-lts-headers linux-zen-headers linux-hardened-headers", 1);
	lh_item v[64];

	/* 1. AMD (Ryzen), kernel linux, GRUB */
	cenario(AMD, "AuthenticAMD", "AMD Ryzen 5 5600X 6-Core Processor", "6.10.8-arch1-1", "", "50000000000");
	lj_catalogo *c = lj_catalogo_ler();
	CONFERE(c != NULL, "catálogo");
	if (!c) {
		return 1;
	}
	lh_hw *h = lh_detectar();
	CONFERE(h->ngpu == 1 && !strcmp(h->gpus[0].vendor, "amd") && !strcmp(h->gpus[0].nome, "Radeon RX 6600/6600 XT/6600M") && !strcmp(h->gpus[0].pci, "1002:73ff"), "a placa AMD (%d, %s, %s, %s)", h->ngpu, h->gpus[0].vendor, h->gpus[0].nome, h->gpus[0].pci);
	CONFERE(!strcmp(h->cpu_vendor, "amd") && strstr(h->cpu, "Ryzen 5 5600X"), "o processador (%s, %s)", h->cpu_vendor, h->cpu);
	CONFERE(!strcmp(h->kernel_pkg, "linux") && !strcmp(h->kernel_uname, "6.10.8-arch1-1"), "o kernel em uso (%s)", h->kernel_pkg);
	CONFERE(!strcmp(h->boot_nome, "grub") && h->uefi && h->boot_livre == 50000000000LL, "boot: %s uefi=%d livre=%ld", h->boot_nome, h->uefi, (long)h->boot_livre);
	CONFERE(!strcmp(h->arch, "x86_64") || h->arch[0], "arquitetura %s", h->arch);
	int n = lh_itens(h, c, v, 64);
	CONFERE(n == 21, "21 itens na tabela (%d)", n);
	QUAL("mesa", LH_COMPATIVEL);
	QUAL("vulkan-radeon", LH_COMPATIVEL);
	QUAL("libva-mesa-driver", LH_COMPATIVEL);
	CONFERE(ITEM("nvidia") && ITEM("nvidia")->compat == LH_INCOMPATIVEL && ITEM("nvidia")->nao_aplica && strstr(ITEM("nvidia")->motivo, "NVIDIA"), "numa máquina AMD o driver NVIDIA não se aplica");
	CONFERE(ITEM("vulkan-intel")->nao_aplica && ITEM("intel-ucode")->nao_aplica, "nem o Vulkan Intel nem o microcódigo Intel");
	QUAL("amd-ucode", LH_COMPATIVEL);
	CONFERE(ITEM("mesa")->instalado && !ITEM("vulkan-radeon")->instalado, "instalado: mesa sim, vulkan-radeon não");
	CONFERE(ITEM("linux")->instalado && ITEM("linux")->em_uso, "linux é o kernel em uso");
	QUAL("linux-lts", LH_COMPATIVEL);
	CONFERE(!strcmp(ITEM("linux-lts")->junto, "linux-lts-headers") && !ITEM("linux-lts")->em_uso, "o linux-lts leva os headers junto");
	QUAL("lib32-mesa", LH_COMPATIVEL);
	CONFERE(lh_pode_instalar(ITEM("vulkan-radeon"), false) && !lh_pode_instalar(ITEM("mesa"), false), "pode instalar o que é compatível e falta; não o que já está");
	CONFERE(!lh_pode_instalar(ITEM("nvidia"), false) && lh_pode_instalar(ITEM("nvidia"), true), "o incompatível só no modo avançado");
	char motivo[256];
	CONFERE(!lh_pode_remover(ITEM("linux"), h, c, motivo, sizeof(motivo)) && strstr(motivo, "em uso"), "não remove o kernel em uso (%s)", motivo);
	CONFERE(!lh_pode_remover(ITEM("mesa"), h, c, motivo, sizeof(motivo)) && strstr(motivo, "base gráfica"), "não remove o mesa com placa de vídeo (%s)", motivo);
	lh_liberar(h);
	lj_catalogo_liberar(c);

	/* 2. NVIDIA RTX 3060 + Intel, kernel linux */
	cenario(NV3060, "GenuineIntel", "12th Gen Intel(R) Core(TM) i5-12400F", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	CONFERE(h->ngpu == 1 && !strcmp(h->gpus[0].vendor, "nvidia") && !strcmp(h->gpus[0].nome, "GeForce RTX 3060") && h->gpus[0].geracao == 3, "RTX 3060: geração %d", h->gpus[0].geracao);
	n = lh_itens(h, c, v, 64);
	QUAL("nvidia-open", LH_COMPATIVEL);
	QUAL("nvidia", LH_COMPATIVEL);
	QUAL("nvidia-utils", LH_COMPATIVEL);
	QUAL("nvidia-settings", LH_COMPATIVEL);
	QUAL("vulkan-nouveau", LH_COMPATIVEL);
	QUAL("nvidia-lts", LH_INCOMPATIVEL);
	CONFERE(strstr(ITEM("nvidia-lts")->motivo, "linux-lts") && strstr(ITEM("nvidia-lts")->motivo, "linux"), "nvidia-lts diz que é para o linux-lts: %s", ITEM("nvidia-lts")->motivo);
	QUAL("nvidia-dkms", LH_VERIFIQUE);
	CONFERE(!strcmp(ITEM("nvidia-dkms")->junto, "linux-headers"), "o dkms leva os headers do kernel em uso (%s)", ITEM("nvidia-dkms")->junto);
	QUAL("lib32-nvidia-utils", LH_VERIFIQUE);
	CONFERE(strstr(ITEM("lib32-nvidia-utils")->motivo, "multilib"), "sem o pacote de 32 bits o motivo é o multilib");
	CONFERE(ITEM("vulkan-radeon")->nao_aplica && ITEM("amd-ucode")->nao_aplica, "nada de AMD aqui");
	QUAL("intel-ucode", LH_COMPATIVEL);
	lh_liberar(h);
	lj_catalogo_liberar(c);

	/* 3. NVIDIA GTX 1060 (Pascal) */
	cenario(NV1060, "GenuineIntel", "Intel(R) Core(TM) i7-7700", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	CONFERE(h->gpus[0].geracao == 2, "GTX 1060 é Pascal (geração %d)", h->gpus[0].geracao);
	n = lh_itens(h, c, v, 64);
	QUAL("nvidia", LH_INCOMPATIVEL);
	CONFERE(strstr(ITEM("nvidia")->motivo, "Pascal") && strstr(ITEM("nvidia")->motivo, "nouveau"), "o motivo cita Pascal e o nouveau: %s", ITEM("nvidia")->motivo);
	QUAL("nvidia-open", LH_INCOMPATIVEL);
	QUAL("vulkan-nouveau", LH_INCOMPATIVEL);
	QUAL("mesa", LH_COMPATIVEL);
	CONFERE(!lh_pode_instalar(ITEM("nvidia"), false), "não instala o driver que a placa não suporta");
	lh_liberar(h);
	lj_catalogo_liberar(c);

	/* 4. híbrido Intel + NVIDIA (notebook) */
	cenario(HIBRIDO, "GenuineIntel", "12th Gen Intel(R) Core(TM) i7-12700H", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	CONFERE(h->ngpu == 2 && !strcmp(h->gpus[0].vendor, "intel") && !strcmp(h->gpus[1].vendor, "nvidia") && h->gpus[1].geracao == 3, "duas placas (%d)", h->ngpu);
	n = lh_itens(h, c, v, 64);
	QUAL("vulkan-intel", LH_COMPATIVEL);
	QUAL("nvidia-open", LH_COMPATIVEL);
	QUAL("intel-media-driver", LH_VERIFIQUE);
	lh_liberar(h);
	lj_catalogo_liberar(c);

	/* 5. sem placa reconhecida (máquina virtual) e uma NVIDIA que não sei de que geração é */
	cenario("00:02.0 VGA compatible controller [0300]: Red Hat, Inc. Virtio 1.0 GPU [1af4:1050] (rev 01)\n", "GenuineIntel", "QEMU Virtual CPU", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	CONFERE(h->ngpu == 1 && !strcmp(h->gpus[0].vendor, "outro"), "placa de máquina virtual: outro");
	n = lh_itens(h, c, v, 64);
	QUAL("mesa", LH_COMPATIVEL);
	lh_liberar(h);
	lj_catalogo_liberar(c);
	cenario("01:00.0 VGA compatible controller [0300]: NVIDIA Corporation Coisa [Mystery Card] [10de:ffff]\n", "AuthenticAMD", "AMD EPYC", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	CONFERE(h->gpus[0].geracao == 0, "geração desconhecida");
	n = lh_itens(h, c, v, 64);
	QUAL("nvidia", LH_VERIFIQUE);
	lh_liberar(h);
	lj_catalogo_liberar(c);
	cenario("", "GenuineIntel", "QEMU", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	n = lh_itens(h, c, v, 64);
	CONFERE(h->ngpu == 0, "sem placa nenhuma");
	QUAL("mesa", LH_VERIFIQUE);
	lh_liberar(h);
	lj_catalogo_liberar(c);

	/* 6. kernels: pouco espaço, kernel em uso lts, remover, driver pré-compilado de outro kernel */
	cenario(NV3060, "GenuineIntel", "Intel i5", "6.10.8-arch1-1", "", "100000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	n = lh_itens(h, c, v, 64);
	QUAL("linux-lts", LH_INCOMPATIVEL);
	CONFERE(strstr(ITEM("linux-lts")->motivo, "/boot") && strstr(ITEM("linux-lts")->motivo, "95 MB"), "pouco espaço em /boot: %s", ITEM("linux-lts")->motivo);
	CONFERE(!lh_pode_instalar(ITEM("linux-lts"), false) && lh_pode_instalar(ITEM("linux-lts"), true), "bloqueia, e o modo avançado libera");
	lh_liberar(h);
	lj_catalogo_liberar(c);
	cenario(NV3060, "GenuineIntel", "Intel i5", "6.10.8-arch1-1", "nvidia", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	n = lh_itens(h, c, v, 64);
	QUAL("linux-zen", LH_VERIFIQUE);
	CONFERE(strstr(ITEM("linux-zen")->motivo, "nvidia-dkms"), "com o nvidia pré-compilado, o zen avisa do dkms: %s", ITEM("linux-zen")->motivo);
	QUAL("linux-lts", LH_VERIFIQUE);
	lh_liberar(h);
	lj_catalogo_liberar(c);
	/* em uso o lts, com dois kernels instalados */
	cenario(NV3060, "GenuineIntel", "Intel i5", "6.6.50-1-lts", "linux-lts nvidia-lts", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	CONFERE(!strcmp(h->kernel_pkg, "linux-lts"), "o lts em uso (%s)", h->kernel_pkg);
	n = lh_itens(h, c, v, 64);
	CONFERE(ITEM("linux-lts")->em_uso && ITEM("linux-lts")->instalado && !ITEM("linux")->em_uso, "linux-lts em uso, linux não");
	QUAL("nvidia-lts", LH_COMPATIVEL);
	QUAL("nvidia", LH_INCOMPATIVEL);
	CONFERE(!lh_pode_remover(ITEM("linux-lts"), h, c, motivo, sizeof(motivo)), "o lts em uso não sai");
	CONFERE(lh_pode_remover(ITEM("linux"), h, c, motivo, sizeof(motivo)), "o linux sai, porque há outro kernel (e não é o em uso)");
	lh_liberar(h);
	lj_catalogo_liberar(c);
	/* um kernel só */
	cenario(AMD, "AuthenticAMD", "AMD Ryzen", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	n = lh_itens(h, c, v, 64);
	CONFERE(!lh_pode_remover(ITEM("linux"), h, c, motivo, sizeof(motivo)), "o único kernel não sai");
	lh_liberar(h);
	lj_catalogo_liberar(c);
	/* sem gerenciador de boot reconhecido */
	char *grub = g_build_filename(dir, "grub", "grub.cfg", NULL);
	unlink(grub);
	g_free(grub);
	cenario(AMD, "AuthenticAMD", "AMD Ryzen", "6.10.8-arch1-1", "", "50000000000");
	c = lj_catalogo_ler();
	h = lh_detectar();
	CONFERE(h->boot_nome[0] == '\0', "sem GRUB nem systemd-boot");
	n = lh_itens(h, c, v, 64);
	QUAL("linux-lts", LH_VERIFIQUE);
	CONFERE(strstr(ITEM("linux-lts")->motivo, "gerenciador de boot"), "pede conferir o boot: %s", ITEM("linux-lts")->motivo);
	lh_liberar(h);
	lj_catalogo_liberar(c);

	/* 7. conferir o boot depois de instalar um kernel */
	escreve("grub/grub.cfg", "menuentry 'Arch' { linux /vmlinuz-linux }\n");
	cenario(AMD, "AuthenticAMD", "AMD Ryzen", "6.10.8-arch1-1", "", "50000000000");
	h = lh_detectar();
	lh_checagem ch[4];
	int k = lh_verificar_kernel(h, "linux-lts", ch, 4);
	CONFERE(k == 3 && !ch[0].ok && !ch[1].ok && !ch[2].ok, "nada em /boot ainda: tudo falha");
	escreve("vmlinuz-linux-lts", "k");
	escreve("initramfs-linux-lts.img", "i");
	k = lh_verificar_kernel(h, "linux-lts", ch, 4);
	CONFERE(ch[0].ok && ch[1].ok && !ch[2].ok && strstr(ch[2].texto, "grub-mkconfig"), "kernel e initramfs ok, GRUB sem a entrada: %s", ch[2].texto);
	escreve("grub/grub.cfg", "menuentry 'Arch LTS' { linux /vmlinuz-linux-lts }\n");
	k = lh_verificar_kernel(h, "linux-lts", ch, 4);
	CONFERE(ch[0].ok && ch[1].ok && ch[2].ok, "com a entrada no grub.cfg, tudo ok");
	lh_liberar(h);
	unlink((g_build_filename(dir, "grub", "grub.cfg", NULL)));
	escreve("loader/loader.conf", "default arch\n");
	h = lh_detectar();
	CONFERE(!strcmp(h->boot_nome, "systemd-boot"), "systemd-boot detectado");
	k = lh_verificar_kernel(h, "linux-lts", ch, 4);
	CONFERE(!ch[2].ok && strstr(ch[2].texto, "systemd-boot"), "systemd-boot sem entrada: %s", ch[2].texto);
	escreve("loader/entries/arch-lts.conf", "title Arch LTS\nlinux /vmlinuz-linux-lts\ninitrd /initramfs-linux-lts.img\n");
	k = lh_verificar_kernel(h, "linux-lts", ch, 4);
	CONFERE(ch[2].ok, "com a entrada em loader/entries, ok");
	lh_liberar(h);

	if (falhas) {
		fprintf(stderr, "%d falhas\n", falhas);
		return 1;
	}
	printf("loja (drivers e kernels): ok\n");
	return 0;
}
