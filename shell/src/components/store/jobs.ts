import { reactive } from "vue";
import { on, type StoreSource } from "../../system";

/** Andamento de cada operação da Loja, pela chave `fonte:nome` (ou `upgrade:fonte`). */
export interface JobState {
  running: boolean;
  line: string;
  error: string | null;
}

export const jobs = reactive<Record<string, JobState>>({});
export const keyOf = (p: { source: StoreSource; name: string }) => `${p.source}:${p.name}`;
export const anyRunning = () => Object.values(jobs).some((j) => j.running);

/** Roda a operação guardando o andamento. `true` se deu certo. */
export async function runJob(key: string, action: () => Promise<void>): Promise<boolean> {
  jobs[key] = { running: true, line: "", error: null };
  try {
    await action();
    delete jobs[key];
    return true;
  } catch (e) {
    jobs[key] = { running: false, line: "", error: String(e) };
    return false;
  }
}

/** Cada linha do pacman/flatpak/makepkg chega aqui. */
export async function followJobs() {
  return on<{ key: string; line: string }>("lumna://store-job", ({ key, line }) => {
    const job = jobs[key];
    if (job?.running) job.line = line;
  });
}
