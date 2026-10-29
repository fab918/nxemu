# Readback completion at Normal accuracy

`generate.py` extracts the production `SubmitFence` method and query completion
callback into a small fixture with manually controlled GPU completion. This
checks that Normal cannot publish a completion ahead of pending readbacks or an
older fence, including a fence already removed from the queue by its worker.
The callback must retain its query slot until retirement and release it once,
including when invalidated. Empty Normal submissions and explicit empty waits
retain their existing fast paths; High still defers ordinary completion.

The unmodified parent code fails the pending-readback assertion; the corrected
code passes all cases. The test checks ordering and query ownership, not
rendering or performance.
The ENDER MAGNOLIA capture contains intermittently absent draws in Normal;
the same scene was stable at High. After installing the corrected module,
the user confirmed that the flickering disappeared at Normal as well.
