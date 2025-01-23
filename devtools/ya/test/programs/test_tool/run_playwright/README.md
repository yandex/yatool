# Playwright

Test-runner for Playwright tests - prepares and executes the command in execution node, parses the results and publishes the test-run info to show in the autocheck UI.

## Browsers bundles

To use browsers in autocheck one need to have Sandbox-resources.
Currently we have a resource [sbr:6034873826](https://sandbox.yandex-team.ru/resource/6034873826/view) for linux, containing:

- chromium-1097
- ffmpeg-1009
- firefox-1438
- libraries required for the browsers in Ubuntu 20

If you need other browsers then you could pack and upload a resource to use it instead.
Browsers also require specific native libraries to be in the node that will execute tests, so to be able to run the tests in default distbuild node one need to pack the libraries as well to the browser resource.

Playwright browsers could be installed locally, then one could find those in [Cache directory (location depends on the OS)](https://github.com/microsoft/playwright/blob/38fc74db7c24398095632fa2e65e913c80df99f4/packages/playwright-core/src/server/registry/index.ts#L274) under `ms-playwright`.

Other way is to download those directly, [templates for CDN links could be found here](https://github.com/microsoft/playwright/blob/38fc74db7c24398095632fa2e65e913c80df99f4/packages/playwright-core/src/server/registry/index.ts#L77), those require to know the browser versions.

List of native libraries required [could be found here](https://github.com/microsoft/playwright/blob/main/packages/playwright-core/src/server/registry/nativeDeps.ts). The libraries in their order might requie some other ones. All the libs should be copied to `required-ubuntu-x86-64-libs` directory next to browsers and packed along with them.

1. Enter the directory containing the browsers
    `cd ~/.cache/ms-playwright`
2. Create list of libraries required
    `/tmp/libs.list`
3. Create a directory for the libs
    `mkdir required-ubuntu-x86-64-libs`
4. Copy all the libs by the list into the dir
    `while read -r f; do  find /usr/lib -name "$f" -exec cp -v -- {} required-ubuntu-x86-64-libs/ \;; done < /tmp/libs.list`

5. Pack and upload the resource:

    ```sh
    tar cjf playwright-browsers.tgz ./chromium-1097 ./ffmpeg-1009 ./firefox-1438 ./required-ubuntu-x86-64-libs
    ya upload playwright-browsers.tgz
    ```

The resource need to be set in test module ya.make:

```yaml
DATA(sbr://6034873826)
```
