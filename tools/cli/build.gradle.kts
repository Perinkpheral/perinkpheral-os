// SPDX-License-Identifier: MIT
// The perINKpheral command line, tools/perinkpheral: build, check and run an application,
// on a device or in the simulator; make a catalog; and what the firmware and the project
// make of their sources - the host's screens before flashing, and the generators. It calls
// the core for everything the CLion plugin knows too.
plugins {
    id("org.jetbrains.kotlin.jvm")
}

group = "dev.perinkpheral"
version = "0.1.0"

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation("dev.perinkpheral:perinkpheral-core:0.1.0")
    // The fonts' outlines, TrueType and CFF, for the generators of the firmware's fonts and
    // glyph packs (Apache 2.0, Java alone). The core does not take it: the CLion plugin has no
    // use for it.
    implementation("org.apache.pdfbox:fontbox:3.0.3")
}

// One jar with the core and Kotlin's library in it: build/libs/perinkpheral.jar.
tasks.register<Jar>("cli") {
    archiveFileName.set("perinkpheral.jar")
    // The launcher publishes this directory's completed archive with an atomic rename.
    providers.gradleProperty("cliJarDirectory").orNull?.let { destinationDirectory.set(file(it)) }
    manifest {
        attributes("Main-Class" to "dev.perinkpheral.cli.MainKt")
    }
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    from(sourceSets.main.get().output)
    dependsOn(configurations.runtimeClasspath)
    from({ configurations.runtimeClasspath.get().map { if (it.isDirectory) it else zipTree(it) } }) {
        exclude("META-INF/*.SF", "META-INF/*.DSA", "META-INF/*.RSA", "META-INF/versions/**")
    }
}

val pageTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.PageTest")
}
tasks.check { dependsOn(pageTest) }
val flashManifestTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.FlashManifestTest")
}
tasks.check { dependsOn(flashManifestTest) }
val simTestMocksTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.SimTestMocksTest")
}
tasks.check { dependsOn(simTestMocksTest) }
val simButtonsTimeTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.SimButtonsTimeTest")
}
tasks.check { dependsOn(simButtonsTimeTest) }
val launcherTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.LauncherTest")
}
tasks.check { dependsOn(launcherTest) }
val argumentsTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.ArgumentsTest")
}
tasks.check { dependsOn(argumentsTest) }
tasks.test { failOnNoDiscoveredTests.set(false) }

val contractCommandTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.ContractCommandTest")
}
tasks.check { dependsOn(contractCommandTest) }

val manualPollTest by tasks.registering(JavaExec::class) {
    dependsOn(tasks.testClasses)
    classpath = sourceSets.test.get().runtimeClasspath
    mainClass.set("dev.perinkpheral.cli.ManualPollTest")
}
tasks.check { dependsOn(manualPollTest) }
