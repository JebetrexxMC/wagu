<?php

/**
 * MP NetWire plugin for RaspAP (github.com/JebetrexxMC/raspap-webgui, plugin format of JebetrexxMC/plugins)
 *
 * Shows the wires mpnetd watches on this hotspot (http://127.0.0.1:8090/wires.json) and starts / stops
 * the mpnet-watch and mpnet-relay services that `mpctl netwire` installs.
 *
 * @see src/RaspAP/Plugins/PluginInterface.php
 */

namespace RaspAP\Plugins\MPNetWire;

use RaspAP\Plugins\PluginInterface;
use RaspAP\UI\Sidebar;

class MPNetWire implements PluginInterface
{
    private string $pluginPath;
    private string $pluginName;
    private const SERVICES = ['mpnet-watch.service', 'mpnet-relay.service'];
    private const STATUS_URL = 'http://127.0.0.1:8090/wires.json';

    public function __construct(string $pluginPath, string $pluginName)
    {
        $this->pluginPath = $pluginPath;
        $this->pluginName = $pluginName;
    }

    public function initialize(Sidebar $sidebar): void
    {
        $sidebar->addItem(_('MP NetWire'), 'fas fa-gamepad', 'plugin__' . $this->getName(), 66);
    }

    public function handlePageAction(string $page): bool
    {
        if (strpos($page, '/plugin__' . $this->getName()) !== 0) {
            return false;
        }
        $status = new \RaspAP\Messages\StatusMessage;
        if (!RASPI_MONITOR_ENABLED && $_SERVER['REQUEST_METHOD'] === 'POST') {
            foreach (['start', 'stop', 'restart'] as $verb) {
                foreach (self::SERVICES as $svc) {
                    $key = $verb . '_' . str_replace(['.service', '-'], ['', '_'], $svc);
                    if (isset($_POST[$key])) {
                        exec('sudo /bin/systemctl ' . $verb . ' ' . escapeshellarg($svc), $out, $rc);
                        $status->addMessage(sprintf('%s %s: %s', ucfirst($verb), $svc, $rc === 0 ? 'ok' : 'failed'), $rc === 0 ? 'success' : 'danger');
                    }
                }
            }
        }
        $services = [];
        foreach (self::SERVICES as $svc) {
            exec('/bin/systemctl is-active ' . escapeshellarg($svc), $o, $rc);
            $services[$svc] = $rc === 0 ? 'up' : 'down';
            $o = [];
        }
        $wires = $this->loadWires();
        $__template_data = [
            'title' => _('MP NetWire'),
            'icon' => 'fas fa-gamepad',
            'action' => 'plugin__' . $this->getName(),
            'pluginName' => $this->getName(),
            'services' => $services,
            'wires' => $wires,
        ];
        echo $this->renderTemplate('main', compact('status', '__template_data'));
        return true;
    }

    /** wires.json from the local mpnetd watch, or null when it isn't running */
    public function loadWires(): ?array
    {
        $ctx = stream_context_create(['http' => ['timeout' => 2]]);
        $raw = @file_get_contents(self::STATUS_URL, false, $ctx);
        if ($raw === false) {
            return null;
        }
        $d = json_decode($raw, true);
        return is_array($d) ? $d : null;
    }

    public function renderTemplate(string $templateName, array $__data = []): string
    {
        $templateFile = "{$this->pluginPath}/{$this->getName()}/templates/{$templateName}.php";
        if (!file_exists($templateFile)) {
            return "Template file {$templateFile} not found.";
        }
        extract($__data);
        ob_start();
        include $templateFile;
        return ob_get_clean();
    }

    public static function getName(): string
    {
        return basename(str_replace('\\', '/', static::class));
    }
}
