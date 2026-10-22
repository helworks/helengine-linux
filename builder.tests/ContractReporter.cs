using helengine.baseplatform.Builders;
using helengine.baseplatform.Reporting;

/// <summary>Collects test progress while turning reported package errors into visible test failures.</summary>
internal sealed class ContractReporter : IPlatformBuildProgressReporter, IPlatformBuildDiagnosticReporter {
    /// <summary>Accepts progress from the real assets-only builder path.</summary>
    public void Report(PlatformBuildProgressUpdate update) { }
    /// <summary>Rejects reported diagnostics rather than hiding a failed dependency behind an assets-only success.</summary>
    public void Report(PlatformBuildDiagnostic diagnostic) { throw new InvalidOperationException(diagnostic.Message); }
}
