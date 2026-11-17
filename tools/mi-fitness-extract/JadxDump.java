import jadx.api.*;
import java.io.*;
import java.nio.file.*;
import java.util.regex.*;

public class Dump {
    public static void main(String[] a) throws Exception {
        JadxArgs args = new JadxArgs();
        for (String f : a[0].split(",")) args.getInputFiles().add(new File(f));
        args.setOutDir(new File(a[1]));
        args.setShowInconsistentCode(true);
        args.setThreadsCount(4);
        Pattern p = Pattern.compile(a[2]);
        try (JadxDecompiler d = new JadxDecompiler(args)) {
            d.load();
            int n = 0;
            for (JavaClass c : d.getClasses()) {
                String name = c.getFullName();
                if (!p.matcher(name).find()) continue;
                Path out = Paths.get(a[1], name + ".java");
                Files.writeString(out, c.getCode());
                n++;
            }
            System.out.println("dumped " + n);
        }
    }
}
