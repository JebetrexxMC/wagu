<?php $d = $__template_data; $w = $d['wires']; ?>
<div class="row">
  <div class="col-lg-12">
    <div class="card">
      <div class="card-header"><i class="<?php echo $d['icon']; ?> me-2"></i><?php echo htmlspecialchars($d['title']); ?></div>
      <div class="card-body">
        <?php $status->showMessages(); ?>
        <form role="form" action="<?php echo $d['action']; ?>" method="POST">
          <?php echo \RaspAP\Tokens\CSRF::hiddenField(); ?>
          <h5><?php echo _('Services'); ?></h5>
          <table class="table table-sm">
            <?php foreach ($d['services'] as $svc => $st) : $k = str_replace(['.service', '-'], ['', '_'], $svc); ?>
            <tr>
              <td><i class="fas fa-circle service-status-<?php echo $st; ?>"></i> <?php echo htmlspecialchars($svc); ?></td>
              <td class="text-end">
                <?php if (!RASPI_MONITOR_ENABLED) : ?>
                  <?php if ($st === 'down') : ?>
                  <input type="submit" class="btn btn-sm btn-success" name="start_<?php echo $k; ?>" value="<?php echo _('Start'); ?>" />
                  <?php else : ?>
                  <input type="submit" class="btn btn-sm btn-warning" name="restart_<?php echo $k; ?>" value="<?php echo _('Restart'); ?>" />
                  <input type="submit" class="btn btn-sm btn-outline-danger" name="stop_<?php echo $k; ?>" value="<?php echo _('Stop'); ?>" />
                  <?php endif; ?>
                <?php endif; ?>
              </td>
            </tr>
            <?php endforeach; ?>
          </table>
        </form>

        <h5><?php echo _('Wires'); ?></h5>
        <?php if ($w === null) : ?>
          <p class="text-muted"><?php echo _('mpnet-watch is not answering on :8090. Install it with: mpctl netwire'); ?></p>
        <?php else : ?>
          <p><?php printf('%d wires, %d up, %d green. Updated %s', $w['total'] ?? 0, $w['up'] ?? 0, $w['green'] ?? 0, htmlspecialchars($w['updated'] ?? '')); ?></p>
          <table class="table table-sm table-striped">
            <thead><tr><th><?php echo _('Wire'); ?></th><th><?php echo _('Group'); ?></th><th><?php echo _('Address'); ?></th><th><?php echo _('Ping'); ?></th><th><?php echo _('Loss'); ?></th><th><?php echo _('Grade'); ?></th></tr></thead>
            <tbody>
            <?php foreach (($w['wires'] ?? []) as $x) : ?>
              <tr>
                <td><?php echo htmlspecialchars($x['name'] ?? $x['id']); ?></td>
                <td><?php echo htmlspecialchars($x['group'] ?? ''); ?></td>
                <td><code><?php echo htmlspecialchars(($x['host'] ?? '') . ':' . ($x['port'] ?? '')); ?></code></td>
                <td><?php echo isset($x['rtt']) ? round($x['rtt']) . ' ms' : '-'; ?></td>
                <td><?php echo isset($x['loss']) ? round($x['loss']) . '%' : '-'; ?></td>
                <td><?php echo htmlspecialchars($x['grade'] ?? 'down'); ?></td>
              </tr>
            <?php endforeach; ?>
            </tbody>
          </table>
        <?php endif; ?>
      </div>
    </div>
  </div>
</div>
