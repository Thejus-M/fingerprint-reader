/*
 * Run one PAM auth attempt against a named service, the way the real consumer
 * of that service does, and report which module answered.
 *
 * Point it at "sddm" to prove the greeter stack takes the fingerprint, without
 * restarting SDDM and losing the graphical session.
 *
 * A password prompt means the fingerprint did NOT satisfy the stack and PAM
 * fell through; the prompt is refused rather than answered, so the test never
 * needs the password typed and never counts a password login as a pass.
 *
 * Build:  gcc -O2 -o fp-pam-test fp-pam-test.c -lpam
 * Run:    sudo ./fp-pam-test sddm "$USER"
 */

#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int prompted_for_secret = 0;

static int conv_fn(int num_msg, const struct pam_message **msg,
                   struct pam_response **resp, void *appdata)
{
    (void)appdata;

    struct pam_response *replies = calloc((size_t)num_msg, sizeof(*replies));
    if (!replies)
        return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; i++) {
        const char *text = msg[i]->msg ? msg[i]->msg : "";

        switch (msg[i]->msg_style) {
        case PAM_TEXT_INFO:
            printf("  [pam info]  %s\n", text);
            fflush(stdout);
            break;
        case PAM_ERROR_MSG:
            printf("  [pam error] %s\n", text);
            fflush(stdout);
            break;
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            printf("  [pam prompt] %s\n", text);
            printf("  -> refusing the prompt: the stack fell through to a typed secret.\n");
            fflush(stdout);
            prompted_for_secret = 1;
            free(replies);
            return PAM_CONV_ERR;
        default:
            break;
        }
    }

    *resp = replies;
    return PAM_SUCCESS;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <pam-service> <user>\n", argv[0]);
        return 2;
    }

    const char *service = argv[1];
    const char *user = argv[2];
    struct pam_conv conv = { conv_fn, NULL };
    pam_handle_t *pamh = NULL;

    int rc = pam_start(service, user, &conv, &pamh);
    if (rc != PAM_SUCCESS) {
        fprintf(stderr, "pam_start(%s): %s\n", service, pam_strerror(pamh, rc));
        return 1;
    }

    printf("Authenticating user \"%s\" through PAM service \"%s\".\n", user, service);
    printf("Touch the sensor when asked.\n\n");

    rc = pam_authenticate(pamh, 0);
    printf("\npam_authenticate: %s (%d)\n", pam_strerror(pamh, rc), rc);

    if (rc == PAM_SUCCESS)
        printf("PASS — the %s stack authenticated without a typed secret.\n", service);
    else if (prompted_for_secret)
        printf("FAIL — fingerprint did not satisfy the stack; it asked for a secret.\n");
    else
        printf("FAIL — see the message above.\n");

    int end = pam_end(pamh, rc);
    if (end != PAM_SUCCESS)
        fprintf(stderr, "pam_end: %d\n", end);

    return rc == PAM_SUCCESS ? 0 : 1;
}
